# How FuhrerOS was built

This document explains the approach behind the from-scratch FuhrerOS kernel
(NEW_EXPLANATION.md). The earlier Linux-based prototype has its own write-up
in [linux-prototype/EXPLANATION.md](linux-prototype/EXPLANATION.md).

## 1. The goal

NEW_EXPLANATION.md replaced "an adaptive layer on top of Linux" with a
stricter requirement: **Limine hands control to our own kernel, and no Linux
code runs at any point.** On top of that kernel the OS needed user space, a
desktop, networking and storage, plus a research question worth measuring:

> Can a small kernel that *classifies what the user is doing* (interactive,
> I/O-bound, CPU-bound, mixed) and adapts its scheduler and cache to it beat
> fixed policies on a personal computer?

Everything was built milestone by milestone (M0–M18). A milestone only
counted when a test proved it. No result in the docs is estimated: anything
not measured is written as `NOT RUN` or `UNKNOWN - REQUIRES VERIFICATION`.

## 2. Environment and toolchain (M0)

- Windows 11 → WSL 2 (Ubuntu) → QEMU with KVM. Every script prints
  `Acceleration: KVM/TCG/WHPX`, so a slow TCG run is never mistaken for
  a KVM run.
- A GCC 15.2 `x86_64-elf` cross compiler (`toolchain/build-toolchain.sh`)
  and Limine 11 (`toolchain/get-limine.sh`).
- Build outputs live on the WSL filesystem. The 20 GiB FFS0 disk image was
  not sparse on NTFS (F-103, D-106).
- The kernel links **no libgcc**. The red-zone-free multilib could not be
  built (F-102), so the one 128/64-bit division was written by hand, and
  `scripts/check-kernel.sh` rejects any undefined symbol (D-103).

## 3. Building upward, one tested layer at a time

Each layer was built only after the layer below it had a passing self-test.
The `test` boot entry runs all of them and prints `TEST name PASS|FAIL`,
then exits QEMU through the isa-debug-exit port, so `scripts/test.sh` is a
real pass/fail gate.

| layer | approach | proven by |
|---|---|---|
| boot (M1) | Copy the Limine responses; build our own page tables and stack; reclaim bootloader memory | banner, `VERIFY linux-absent` |
| CPU (M2) | GDT/TSS, IDT, symbolised panics (two-pass link with a symbol table), ACPI → LAPIC/IOAPIC, TSC-calibrated 1000 Hz timer | `idt.exception.int3`, `apic.timer.ticks` |
| memory (M3) | Bitmap frame allocator, 4-level paging with NX/WP and guard pages, slab heap | `pmm.*`, `vmm.*`, `heap.*` |
| tasks (M4) | Kernel threads, context switch in assembly, scheduler core in C with policies as C++ classes (D-107) | `sched.concurrent.*` for each policy |
| processes (M5) | Ring 3, ELF loader, `syscall`/`sysret`, native `spawn` ABI instead of fork (D-113), threads, pipes, ports | `usertest`: a faulting process dies, the kernel survives |
| shell (M6, M8) | `sh` with pipes, redirection, history and scripts; 45+ small programs over `libfu` | `usertest` shell pipelines |
| storage (M7) | PCI, virtio-blk with MSI-X, VFS, own filesystem FFS0 (D-111), buffer cache with policies (D-112) | FFS0 create/copy/rename/delete, 1 MiB files |
| network (M9) | virtio-net, Ethernet/ARP/IPv4/ICMP/UDP/DHCP, a TCP subset (D-114), sockets | `nettest`: DHCP, TCP echo, DNS, HTTP to example.com |
| graphics/desktop (M10–M11) | In-kernel compositor with damage tracking and a window manager (D-115); apps draw into shared surfaces | screenshots in `docs/images/` |
| input (M12) | PS/2 keyboard/mouse; gesture recognizer fed synthetic touch frames (D-117) | `gesture.*` self-tests |

The kernel is uniprocessor (D-105). With one CPU, disabling interrupts is
the spinlock, and mutexes cover long sections such as composition. This
removed a whole class of bugs and left the time for the research part.

## 4. The research part (M13–M16)

**Profiler.** Every task records its run-time bursts, voluntary sleeps, I/O
waits and input wakeups. A small rule set assigns it a class: IDLE,
INTERACTIVE, IO_BOUND, CPU_BOUND or MIXED (D-108). A system-level class,
weighted by activity rather than CPU time (D-110, after F-109), is shown live
in the Fuhrer Control Center and the panel.

**Adaptive scheduler.** It maps each class to a priority and quantum:
interactive tasks get short quanta and high priority, CPU-bound tasks get
long quanta and low priority. Aging prevents starvation (D-109). Three fixed
policies (round-robin, priority, low-latency) serve as baselines B0–B2.

**Adaptive buffer cache.** It detects sequential streams per file and
switches between LRU and read-ahead. The first experiments showed read-ahead
never fired, because metadata reads broke up the data stream (F-114). This
was fixed with `bread_meta`.

**Experiments.** `scripts/experiment.sh SUITE` boots a one-off ISO with
`bench=SUITE`. Init runs `/etc/bench/SUITE.sh`, the guest powers itself
off, and `scripts/analyze.py` turns the serial log into `results.jsonl`,
`summary.md` and SVG graphs. Every run records the git revision,
acceleration, VM size and date. The suites are:

- `sched`: latency and throughput per policy, repeated.
- `cache`: policies × access patterns.
- `transition`: how fast the classifier notices a workload change.
- `ablation`: the adaptive scheduler with parts switched off.

Pre-fix runs (E-101..E-104) are kept alongside the post-fix runs. The fixes
they exposed are recorded as failures, so the history is not rewritten.
Results and their limits are in [docs/experiments.md](docs/experiments.md).

## 5. How bugs were handled

Most real bugs were found by the experiments, not by the self-tests. Each
one has a record under [research-log/failures/](research-log/failures/README.md)
with symptom, root cause, fix, regression test and lesson (F-101..F-114).
Examples:

- A console that read write-combining video memory with interrupts off
  (F-104).
- A TCP zero-window deadlock that made adaptive loopback 20x slower
  (F-107, F-108).
- Threads surviving process exit (F-106).

Design choices, including the ones that limit the system, are in
[docs/decisions.md](docs/decisions.md) (D-101..D-118).

## 6. Honest limits

- Tested only in QEMU/KVM. The ISO is a hybrid BIOS/UEFI image, but
  **M18 (real hardware) has NOT RUN**.
- Uniprocessor; no FPU/SSE state in the kernel or user space (D-104).
- The gesture recognizer is tested with synthetic frames; there is no real
  touchpad driver.
- FuhrerWeb is an HTTP page viewer, not a browser engine: no TLS, no
  JavaScript (D-116).
- M17 is partial: no large third-party programs (browser, compiler) were
  ported.
- The benchmarks are synthetic workloads inside one VM. Whether the gains
  carry over to real desktop use is UNKNOWN - REQUIRES VERIFICATION.
