# FuhrerOS — Engineering Decision Log

Format: INSTRUCTION §30 / master prompt §3. Evidence that was not measured is
labelled as such; rationale that is not established is marked
**UNKNOWN — REQUIRES VERIFICATION**.

---

## D-001 — Build on Linux, not a new kernel

**Problem:** INSTRUCTION §4 and Principle 2: the research question concerns
adaptive policy selection, not kernel construction.

**Context:** The repository previously contained a 16-bit real-mode bootloader
experiment (NASM, FAT12 floppy). It is preserved unchanged in `legacy/`.

**Options:** A: extend the hobby kernel. B: Linux + userspace adaptive layer.
C: Linux kernel fork.

**Evidence:** Option A would need memory management, a scheduler, drivers,
filesystems and networking before the first experiment could run (§4 list).
Option B runs unmodified applications immediately (M14 verified: Python,
GCC, Git, Vim, curl, Lynx all pass `fuhrer-selftest`).

**Decision:** B. Kernel changes only if an experiment requires them (M13).

**Tradeoffs:** Policies are limited to what Linux exposes (sysfs/procfs,
io_uring, socket options). Mechanisms inside the kernel (e.g. per-request
path selection) are out of reach until M13.

**Status:** Adopted.

---

## D-002 — Guest distribution: Alpine Linux 3.22 with `linux-virt` 6.12

**Problem:** Choose the Linux userspace/kernel for the VM image.

**Options:**
- A: Alpine (musl, OpenRC, busybox; `linux-virt` kernel tuned for VMs)
- B: Ubuntu/Debian cloud image (glibc, systemd, cloud-init)
- C: Buildroot (fully custom, minimal)
- D: Build our own kernel from source

**Evidence (measured):** Alpine rootfs with the full developer toolchain is
475 MB (build-info), boots to SSH in 22–31 s under nested KVM. Packages
needed for M14 (python3, gcc, git, vim, lynx, w3m, firefox-esr, fio,
liburing) are all in Alpine 3.22 main/community.

**Evidence (not measured):** B would boot slower and carry systemd/cloud-init
complexity; C lacks a package manager (hurts M14); D needs flex/bison/libelf
toolchains and adds a kernel to maintain.

**Decision:** A. OpenRC keeps the service graph small and legible.

**Tradeoffs:** musl libc: glibc-only binaries (e.g. Google Chrome) do not
run; Firefox ESR does. Results tied to kernel 6.12 (Threat T6).

**Validation:** `fuhrer-selftest` compatibility section.

**Status:** Adopted.

---

## D-003 — Reproducible build in Docker; artifacts on a Linux filesystem; direct kernel boot

**Problem:** Build a root filesystem with correct root ownership without
root on the host, and keep VM disks off slow/noisy filesystems.

**Options:** A: build with `sudo` + loop mounts on the host. B: build inside a
Docker container (root inside the container only) using `apk --root` and
`mke2fs -d`. C: debootstrap/fakeroot.

**Decision:** B. `mke2fs -d` populates ext4 from a directory without mounting,
so no loop devices or privileges are needed. QEMU boots the kernel/initramfs
directly (`-kernel/-initrd`); the disk image also contains `/boot`.

Artifacts go to `$HOME/.cache/fuhreros/out` when the repository is on a
Windows drive (`/mnt/*`), because 9p/drvfs does not support `O_DIRECT`
(needed for `cache=none`) and adds host-side latency to every guest I/O.

Each VM uses a qcow2 copy-on-write overlay over the immutable base image, so
`reset.sh` = delete overlay, and the acceptance test verifies the base image
hash is unchanged.

**Tradeoffs:** Needs Docker. Direct kernel boot is not how bare metal boots;
a bootloader is needed for the bare-metal milestone.

**Status:** Adopted.

---

## D-004 — Sampling interval 1 s

**Problem:** How often fuhrerd samples counters.

**Evidence:** Profiler overhead measured by fuhrerd itself (`overhead_pct`
in `fuhrer status`); a full sample reads 5 procfs files plus
`/proc/<pid>/{stat,io}` for every process. The desktop-scale target
(INSTRUCTION D-014 example) changes workload on the order of seconds.

**Decision:** 1000 ms default; configurable (`interval_ms`, 10 ms – 10 min).

**Alternatives rejected:** 10 ms (per-process scan cost scales with process
count; measured in ablation A5 / Graph 4), 10 s (too slow for transitions).

**Validation:** `benchmark.sh --suite ablation` runs 100/1000/5000 ms and
observe-mode overhead at 10/100/1000 ms.

**Status:** Adopted, pending ablation results.

---

## D-005 — Hysteresis: 3 consecutive samples and 3 s minimum dwell

**Problem:** A single noisy sample must not flip kernel-wide settings;
switching has a cost (measured: 20–120 ms per switch, dominated by changing
the block I/O scheduler, which freezes the request queue).

**Decision:** A switch requires `hysteresis` (3) consecutive samples wanting
the same new policy *and* at least `min_dwell_ms` (3000) since the last
switch. Operator-selected (static/observe/off) modes apply immediately.

**Tradeoffs:** Worst-case reaction time ≈ 3 × interval (+ dwell). Short
phases (< ~4 s) are never adapted to — deliberate.

**Validation:** Ablation A7 (hysteresis 1 / 3 / 5).

**Status:** Adopted.

---

## D-006 — Default workload-class → policy map

| class | policy | reasoning |
|---|---|---|
| IDLE, INTERACTIVE, MIXED | NORMAL | no evidence that deviation helps; stock is safest |
| CPU_BOUND | BATCHED | longer EEVDF base slice → fewer preemptions (hypothesis) |
| IO_SEQUENTIAL | BATCHED | large read-ahead, deadline scheduler, deferred writeback |
| IO_RANDOM | SPECIALIZED | read-ahead wastes bandwidth on random access; `none` scheduler; io_uring + O_DIRECT in libfuhrer |
| NETWORK_HEAVY | SPECIALIZED | socket busy-polling for latency |

**Evidence:** Hypotheses only at the time of writing. The map exists to be
confirmed or overturned by M5 static benchmarks (`benchmark.sh --suite main`,
column "B3 best static"). The map is configuration (`map.CLASS = policy`), not
code.

**Status:** Provisional — REQUIRES VERIFICATION by E-series experiments.

---

## D-007 — A policy is a reversible bundle of kernel tunables + a library hint

**Problem:** What can a "policy" change without compromising correctness
(INSTRUCTION M-§10: Correctness > Safety > Reproducibility > Performance)?

**Decision:** Each policy is a column in one knob table
(`adaptive/policy/knobs.c`):

| tunable | NORMAL | BATCHED | SPECIALIZED |
|---|---|---|---|
| block `scheduler` | boot value | mq-deadline | none |
| block `read_ahead_kb` | boot | 4096 | 16 |
| block `nr_requests` | boot | ×2 | boot |
| block `nomerges` | boot | 0 | 1 |
| block `rq_affinity` | boot | boot | 2 |
| `vm.dirty_background_ratio` | boot | 20 | 5 |
| `vm.dirty_ratio` | boot | 40 | boot |
| `vm.dirty_expire/writeback_centisecs` | boot | 6000 / 1500 | boot |
| `net.core.busy_read/busy_poll` | boot | boot | 50 µs |
| `net.core.netdev_budget(_usecs)` | boot | 600 / 8000 | boot |
| EEVDF `base_slice_ns` (debugfs) | boot | ×4 | boot |

and the same id is published (shared memory) so libfuhrer can pick an I/O
backend: synchronous syscalls / batched io_uring / io_uring + O_DIRECT.

**Safety argument:** None of these change durability semantics: `fsync`,
`O_SYNC` and journaling are untouched. Writeback deferral only lengthens the
window for data the application never asked to be durable — the same window
Linux already allows. Originals are saved before the first write and
persisted to `/run/fuhrer/knobs.orig` so a crashed daemon's successor restores
them (unit-tested). Stopping fuhrerd restores everything (acceptance test
`restore-on-stop`).

**Tradeoffs:** System-wide, not per-process (see D-009).

**Status:** Adopted.

---

## D-008 — Classifier features and thresholds

**Decision:** Rule-based (M3; no ML). Sources: `/proc/stat`, `/proc/meminfo`,
`/proc/vmstat`, `/proc/diskstats` (whole physical disks only),
`/proc/net/dev` (non-loopback), `/proc/<pid>/{stat,io}`.

- Sequentiality has no direct counter. Proxy: request merge ratio
  (`merged / (ios + merged)`) and average request size. Block-layer merges
  only happen for adjacent requests.
- Page-cache hits never reach `/proc/diskstats`; `syscr+syscw` from
  `/proc/<pid>/io` catch cached I/O (`cached_small_io`).
- A single thread saturating one vCPU counts as CPU_BOUND even when total
  utilisation is 50 % on 2 vCPUs.

Threshold values (`fuhrer.conf`) are initial guesses —
**UNKNOWN — REQUIRES VERIFICATION** against labelled workloads; the unit
tests only prove the rules are applied as written.

**Known limitation:** `rchar/wchar` include pipes and sockets, so a chatty
IPC workload can look like cached I/O; network is checked first to reduce
this.

**Status:** Adopted, thresholds provisional.

---

## D-009 — One system-wide policy (not per process)

**Problem:** The [ADAPT] log names a PID, but should policy be per process?

**Decision:** System-wide. Block, VM and net tunables are global or per
device; per-process control would need cgroup v2 controllers (io.max,
cpu.weight) or per-socket options, which is future work. The PID in logs is
attribution (top CPU or top I/O process in the interval), not scope.

**Tradeoffs:** Two concurrent workloads with conflicting needs → MIXED →
NORMAL. That is a real limitation for personal computing (browser + build).

**Status:** Adopted for the prototype; revisit after M9.

---

## D-010 — Acceleration: KVM when accessible, else TCG

**Context:** Host is Windows 11 → WSL2 (Hyper-V) → Ubuntu 26.04 with QEMU
10.2. `/dev/kvm` exists in WSL2 (nested virtualisation) but the user was not
in the `kvm` group.

**Decision:** Scripts detect `/dev/kvm` read/write access and print
`Acceleration: KVM` or `Acceleration: TCG`; `FUHRER_ACCEL` overrides. With the
user's approval (sudo password supplied for this purpose) the WSL user was
added to the `kvm` group — a change inside WSL only, no Windows change.

**Tradeoffs:** Nested virtualisation inflates I/O latency and variance
(Threat T1/T4). All comparisons are within one VM configuration.

**Status:** Adopted.

---

## D-011 — Benchmark methodology

- **Same image for every baseline.** B0 (native Linux) boots the identical
  image with `fuhrer.disable=1` (fuhrerd never starts). B1 = fuhrerd static
  NORMAL with libfuhrer normal backend. B2 = adaptive + libfuhrer AUTO.
  B3 = best of the static policies per workload (computed, not guessed).
- **Interleaving:** within a boot, configurations are shuffled per repetition
  so slow host drift affects all configurations alike.
- **Cache hygiene:** data file = 1.5 × guest RAM; caches dropped before each
  storage run; sequential cursor continues from warm-up so the measured phase
  never re-reads warm-up data.
- **Warm-up (5 s) > adaptation time** (3 samples × 1 s) so B2 is measured
  after it has had the chance to adapt; the transition suite measures the
  adaptation period itself.
- Raw per-op latencies (reservoir, ≤ 1 M samples) → p50/p95/p99; per-run
  JSON retained; summary = median across reps with CV%.

**Status:** Adopted.

---

## D-012 — Policy publication through a seqlocked mmap file

**Decision:** fuhrerd writes `/run/fuhrer/state.shm` (policy, class, mode,
switch count) under a sequence lock; libfuhrer maps it read-only and checks it
before each batch without a syscall. Absent daemon → libfuhrer degrades to
NORMAL (unit-tested).

**Alternatives rejected:** UNIX socket queries (syscall per check);
signals to applications (intrusive).

**Status:** Adopted.

---

## D-013 — Development VM conveniences

Serial console auto-logs in as root, and root/fuhrer passwords are `fuhrer`.
This is a local development VM reachable only via QEMU user networking bound
to 127.0.0.1. **Must be removed** before any bare-metal or networked use.

**Status:** Adopted for development only.

---

## D-014 — M7 finding: switching cost is dominated by the I/O scheduler change

**Evidence (measured, E-000 exploratory run in dev VM, 1 sample each):**
NORMAL→BATCHED 22.9 ms, BATCHED→SPECIALIZED 119.8 ms,
SPECIALIZED→BATCHED 40.0 ms, BATCHED→NORMAL 80.6 ms (`switch_ms` in
`adapt.log`). Transitions that change the elevator freeze the block queue.

**Implication:** With a 3 s minimum dwell, worst-case switching time is
< 5 % of dwell; the switching cost does not argue against adaptation at
desktop time scales, but would at sub-second scales.

**Next:** measure transient throughput loss around switches (Graph 3).

**Status:** Recorded; systematic measurement in the ablation suite.
