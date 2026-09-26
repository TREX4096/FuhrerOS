# How FuhrerOS was built: the approach

This document explains *what* was built, *why it was built this way*, and
*how each piece was verified*. For details see `docs/architecture.md`
(design), `docs/decisions.md` (every decision with alternatives) and
`docs/experiments.md` (measured results).

---

## 1. Reading the brief

`INSTRUCTION.md` asks for a research OS: **FuhrerOS**, an OS that detects
the kind of workload it is running and adapts its policies, while still
running normal Linux programs. It sets firm rules:

- **VM first.** Everything must boot and be tested in QEMU. Never touch the
  host disk.
- **Don't write a new kernel unnecessarily.** Use Linux as the foundation
  and put the research contribution on top of it.
- **Rule-based before ML.** Three policies (Normal / Batched / Specialized),
  logged decisions, then measurement.
- **Never invent results.** Every number must come from a real run, with
  its configuration saved.
- **Keep a reasoning log**: decisions, failures and experiments.

So "a full-fledged OS" here means a complete, bootable system that runs
real applications and has the adaptive layer as its distinguishing feature.
It does not mean a from-scratch kernel. The repo's old 16-bit bootloader
experiment was kept, untouched, in `legacy/`.

## 2. The stack

```text
Applications (Python, GCC, Git, Vim, curl, Lynx, Firefox, ...)
        │                         │
        │                  libfuhrer (policy-aware I/O)
        ▼                         ▼
   Linux 6.12 kernel  ◄──── fuhrerd: profile → classify → pick policy → apply
        │
   virtio disk / network
        │
   QEMU + KVM   (inside WSL2 on Windows)
```

- **Base system:** Alpine Linux 3.22 with the `linux-virt` 6.12 kernel. It
  is small, boots in about 20 s, and has every package needed (compilers,
  Python, browsers). Ubuntu (heavier, systemd), Buildroot (no package
  manager) and a custom kernel build were considered and rejected (D-002).
- **Branding and integration:** FuhrerOS `os-release`, login banner, motd,
  prompt, and an OpenRC service for the daemon. It ships a `base` profile
  (console) and a `desktop` profile (Xorg + Openbox + Firefox).

## 3. The adaptive layer (the research part), written in C

| piece | what it does |
|---|---|
| **Profiler** | Every second it reads kernel counters (`/proc/stat`, `diskstats`, `net/dev`, `meminfo`, and per-process `stat`/`io`). |
| **Feature extractor** | Turns counters into rates: CPU %, IOPS, MB/s, average request size, merge ratio (a proxy for sequential access), queue depth, packets/s, I/O syscalls/s, and the top CPU and top I/O process. |
| **Classifier** | Rule-based: IDLE, INTERACTIVE, CPU_BOUND, IO_SEQUENTIAL, IO_RANDOM, NETWORK_HEAVY, MIXED. Each class comes with a reason string. |
| **Engine** | Maps class → policy. It switches only after 3 agreeing samples and 3 s since the last switch, so noise cannot cause flapping. |
| **Policies** | NORMAL = stock Linux. BATCHED = throughput-oriented (large read-ahead, deadline scheduler, deferred writeback, bigger network budget, longer CPU slice). SPECIALIZED = latency-oriented (tiny read-ahead, no I/O scheduler, same-CPU completion, socket busy-polling). |
| **libfuhrer** | For programs that opt in, I/O follows the policy: plain syscalls → batched io_uring → io_uring with O_DIRECT. |
| **`fuhrer` CLI** | `status` dashboard, `profile`, `policy`, `log`, `benchmark`, and an interactive `fuhrer>` shell. |

**Safety was designed in.** A policy only changes tunables that are
reversible and don't affect data safety; fsync and journaling are never
touched. Original values are saved before any change and restored when the
daemon stops, even after a crash, because the originals are persisted in
`/run`. Every switch is written to `/var/log/fuhrer/adapt.log` as an
`[ADAPT]` record with the PID, the metrics, the reason and the switch
latency.

## 4. Build and run pipeline

- **Build in Docker.** No root is needed on the host. The container
  compiles the C code, runs the unit tests, installs Alpine into a
  directory, and writes it into an ext4 disk image with `mke2fs -d`, so no
  loop mounts are needed. It then converts the image to qcow2.
- **Run in QEMU.** Scripts detect KVM (and fall back to TCG), boot the
  kernel with a copy-on-write disk layered over the untouched base image,
  forward SSH to localhost, and log the serial console.
- **Windows support.** Every script re-runs itself inside WSL, and
  `fuhreros.cmd` is a launcher. With your approval (the sudo password in
  `.env`, which is gitignored and never printed), the WSL user was added to
  the `kvm` group, making the VM hardware-accelerated.

Commands: `scripts/build.sh`, `run.sh`, `debug.sh`, `reset.sh`, `ssh.sh`,
`test.sh`, `benchmark.sh`, `build-bootable.sh`, plus `make` targets.

## 5. How it was verified (not just "it compiles")

1. **Unit tests** (69 + 311 checks): parsers, classifier, hysteresis,
   config, knob save/apply/restore including crash recovery, and data
   integrity across every I/O backend combination. They run in the build
   container and again inside the VM, because Docker blocks io_uring and
   only the VM exercises it for real.
2. **`fuhrer-selftest`** inside the VM: disk, virtio, network, SSH, the
   daemon, and apps (Python, GCC, Git, Vim, curl https, Lynx). 24/24 pass.
3. **`scripts/test.sh`** acceptance test covers:
   - boot;
   - the self-test;
   - a live adaptation check (a random-read workload must drive the policy
     to SPECIALIZED);
   - settings restored on stop;
   - clean shutdown;
   - destroy and recreate;
   - the base image's hash unchanged.

   7/7 pass.
4. **Desktop:** booted headless and screenshotted through the QEMU monitor.
   Xorg, the panel, the live dashboard and Firefox loading
   https://example.com were all confirmed (`docs/images/`).

## 6. How it is evaluated (the research part)

`scripts/benchmark.sh` runs the baselines the brief defines. All of them
use the **same image and the same VM size**:

- **B0**: plain Linux (daemon disabled at boot).
- **B1**: FuhrerOS with a fixed NORMAL policy.
- **Static BATCHED and SPECIALIZED.** The best of the three static policies
  per workload is **B3**.
- **B2**: adaptive.
- **A6 decomposition**: "kernel knobs only" vs "library only". This was
  added after the first quick run showed that most of the random-read gain
  came from io_uring keeping 32 requests in flight, not from kernel
  settings. It would be misleading not to separate the two.
- **Transition suite**: idle → CPU → random read → sequential read → idle.
  It measures how fast the system reaches the right policy (Graph 3).
- **Ablations**: monitoring overhead with the profiler off vs sampling at
  10/100/1000 ms (Graph 4), sampling interval 100/1000/5000 ms, and
  hysteresis 1/3/5.

Methodology to keep results honest:

- Configurations are shuffled in each repetition.
- The data file is 1.5× RAM, and caches are dropped before each run.
- Warm-up is longer than the adaptation time.
- Percentiles come from raw per-operation samples.
- Every experiment folder stores `config.json`, system info, raw JSON,
  logs and the daemon's metrics.

Unmeasured values are printed as **NOT RUN**.

## 7. Being wrong on purpose (and recording it)

Initial choices were treated as hypotheses. Example: I assumed sequential
I/O is best served by BATCHED. The first measurements showed SPECIALIZED
(O_DIRECT) reading about 13× faster on this platform. The class → policy
map is configuration, not code, and is revised from the data (D-006). Bugs
found along the way are recorded as F-001…F-005 in
`research-log/failures/`, each with its root cause, fix and lesson.

## 8. Honest limits

- The benchmarks run under **nested virtualization** (Windows → WSL2 →
  KVM). Only relative comparisons within one run are meaningful, not
  absolute speeds.
- Policies are **system-wide**, not per process. Two conflicting workloads
  → MIXED → NORMAL.
- The network benchmarks use loopback, so they don't exercise virtio-net.
- Classifier thresholds are initial guesses. They are tested for
  consistency, not yet tuned against labelled workloads.
- There is no ML yet (deliberately), and no kernel patches yet (M13).
- Bare metal: a self-booting GPT disk (GRUB, BIOS + UEFI, optional generic
  LTS kernel) can be generated with `scripts/build-bootable.sh`. Actually
  writing it to a USB stick is left to you, because it overwrites a whole
  device.
- Dev-VM conveniences (root auto-login, the password `fuhrer`) must be
  removed before any real deployment.
