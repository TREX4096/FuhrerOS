# FuhrerOS — Decision Log (from-scratch kernel)

Format: NEW_EXPLANATION §42. The D-0xx records of the earlier Linux-based
prototype are preserved in `linux-prototype/docs/decisions.md`. Rationale that
was not established is marked **UNKNOWN — REQUIRES VERIFICATION**.

---

## D-101 — Replace the Linux base with a kernel written from scratch
**Problem:** NEW_EXPLANATION requires that FuhrerOS have its own kernel; Linux may only be the development host.
**Context:** The previous prototype was Linux 6.12 + a userspace adaptive layer.
**Options:** (A) keep Linux and rename; (B) fork Linux; (C) new kernel booted by an existing bootloader.
**Decision:** C. Every kernel subsystem is new code in `kernel/`. The prototype is kept, unmodified, in `linux-prototype/` as prior work (its experiments motivated the adaptive-policy design).
**Validation:** `scripts/test.sh` verifies the FuhrerOS banner and the absence of any Linux banner on the serial console; `scripts/check-kernel.sh` rejects kernels with an interpreter, dynamic section, undefined symbols, or Linux-targeted objects.
**Status:** Adopted.

## D-102 — Limine boot protocol, base revision 6, own page tables
**Problem:** Getting from firmware to a 64-bit kernel is not the research contribution (§2, §8).
**Decision:** Limine 11.x (BIOS + UEFI ISO). The kernel requests revision 6, copies every response into its own `struct boot_info`, builds its own GDT/IDT/page tables, moves off the bootloader stack, and then reclaims bootloader memory. Only `limine.h` (0BSD) is used from Limine.
**Tradeoffs:** Base revision 6 guarantees a clean CPU state (CR0/CR4/EFER) but maps MMIO nowhere, so the kernel maps LAPIC/IOAPIC/PCI BARs itself (`ioremap`).
**Status:** Adopted.

## D-103 — Freestanding cross toolchain; kernel links no libgcc
**Decision:** GCC 15.2 + binutils 2.45 for `x86_64-elf`, built from source by `toolchain/build-toolchain.sh`.
**Evidence:** The OSDev multilib patch for a red-zone-free libgcc did not take effect (`-print-multi-lib` shows only `.`), see F-102. A red-zone libgcc routine interrupted in kernel mode could have its locals clobbered.
**Decision (follow-up):** The kernel links **without libgcc**; its only use (128/64-bit division during timer calibration) is replaced by a shift-subtract routine. Any future libgcc dependency is caught as an undefined symbol by `check-kernel.sh`. User programs still link libgcc: interrupts switch them to a kernel stack, so the red zone is safe there.
**Status:** Adopted.

## D-104 — No FPU/SSE state (kernel and user code are general-registers-only)
**Problem:** Using SSE requires saving/restoring FPU state on context switches (FXSAVE/XSAVE, CR4.OSFXSR).
**Decision:** Not yet. Everything is compiled with `-mgeneral-regs-only`; graphics, networking and statistics use integer arithmetic.
**Tradeoffs:** No floating point in user programs (percentiles etc. are integer); porting existing software (a real browser) will require FPU support first.
**Status:** Adopted for now; lifting it is on the path to M15/M17.

## D-105 — Uniprocessor kernel
**Decision:** Only the bootstrap CPU runs FuhrerOS; mutual exclusion is "interrupts off" (`irq_save`) plus sleeping mutexes for long critical sections.
**Tradeoffs:** QEMU offers 2–4 vCPUs; the others idle. Every experiment records this (`vcpus_used_by_kernel: 1`). SMP needs per-CPU run queues, spinlocks and IPIs — future work.
**Status:** Adopted.

## D-106 — Build outputs on the WSL filesystem
**Evidence:** On the Windows drive (`/mnt/d`) the 20 GiB FFS0 image was stored fully allocated (NTFS via drvfs did not keep it sparse) and VM disk I/O went through 9p (F-103).
**Decision:** When the checkout is under `/mnt/*`, the Makefile and scripts put build outputs in `~/.cache/fuhreros/build`. The source stays where the user edits it.
**Deviation:** NEW_EXPLANATION §4 asks for the repository itself in `~/projects/fuhreros`; the user's editor and GitHub checkout are on `D:\SEM7\FuhrerOS`, so only the build artifacts moved. **UNKNOWN — REQUIRES VERIFICATION** whether the user prefers moving the checkout too.
**Status:** Adopted.

## D-107 — Scheduler: C core, C++ policies behind a C interface
**Decision:** `sched/core.c` owns tasks, context switching, sleeping, blocking, preemption and accounting. Policies are C++ classes (`sched/policies.cpp`, `sched/adaptive.cpp`) exported as `struct sched_policy` tables; the spec's `scheduler_init / task_create / task_block / task_wake / task_exit / scheduler_tick / scheduler_select_next` map 1:1 onto `init / task_create / task_block / task_wake / task_exit / tick / select_next`. Policies can be switched at run time (`sched policy NAME`).
**Reason:** One kernel, four policies, same measurement path — required for fair comparisons (§16).
**Status:** Adopted.

## D-108 — Per-task workload classification (profiler)
**Decision:** Every window (100 ms default) each task is classified from its own counters (CPU share, burst length, voluntary blocks, I/O syscalls, preemptions) by ordered rules: IDLE, IO_BOUND, INTERACTIVE, CPU_BOUND, MIXED. A class change needs `hysteresis` (2) consecutive agreeing windows. "Confidence" = share of the last 8 windows agreeing with the current class — a stability measure, **not a probability**.
**Evidence:** Self-test `profiler.classify.*`: a spinning thread → CPU_BOUND and a 3 ms sleeper → INTERACTIVE within 700 ms (both 87 %).
**Status:** Adopted; thresholds are initial values (ablations A6/A7 vary window and hysteresis).

## D-109 — Adaptive policy parameters
| class | priority | quantum | wake-preempt |
|---|---|---|---|
| INTERACTIVE | 4 | 3 ms | yes |
| IO_BOUND | 8 | 5 ms | yes |
| MIXED / UNKNOWN / IDLE | 12 | 8 ms | no |
| CPU_BOUND | 20 | 30 ms | no |

Tasks waiting > 100 ms in the run queue are lifted to priority 4 for one quantum (anti-starvation; was 12 until F-115). User base priority shifts the band by (base−16)/4.
**Status:** Adopted; evaluated in E-10x.

## D-110 — System-level class weighted by activity, not CPU time
**Evidence:** In the first transition run (E-101 pilot, discarded), the interactive and network phases read as IDLE: tasks that mostly block used < 5 ms of CPU per window, so a CPU-time-weighted vote ignored them (F-109).
**Decision:** Weight = CPU ms + wake-ups + I/O operations in the last window; IDLE only below a floor of `window_ms/10 + 2`.
**Status:** Adopted.

## D-111 — FFS0, a deliberately simple native filesystem
**Decision:** 4 KiB blocks; superblock, block bitmap, inode bitmap, 128-byte inodes (12 direct + single + double indirect), directories as arrays of 64-byte entries. All I/O goes through the buffer cache. No journal.
**Tradeoffs:** Crash consistency is not guaranteed (a crash can leak blocks); the mount prints a warning when the clean flag is not set. Truncation to a non-zero smaller size keeps tail blocks allocated.
**Status:** Adopted.

## D-112 — Buffer cache with pluggable policies
**Decision:** LRU, FIFO, CLOCK, LRU+read-ahead, and ADAPTIVE (stream classification every 256 accesses: SEQUENTIAL → aggressive read-ahead + evict-behind; RANDOM → no read-ahead, CLOCK if a working set is detected; MIXED → modest read-ahead). Write-back with a 2 s flusher.
**Status:** Adopted; evaluated in the cache suite.

## D-113 — Native process ABI: spawn, not fork/exec
**Decision:** `spawn(path, argv, stdio[3])` creates a process with its own address space; threads via `thread_create`. No `fork`.
**Reason:** §18 asks for a clean native ABI first; spawn avoids copy-on-write machinery.
**Status:** Adopted.

## D-114 — TCP subset
**Decision:** Handshakes, sliding window, cumulative ACKs, go-back-N retransmission with exponential back-off, fast retransmit on 3 duplicate ACKs, zero-window persist probes, window updates, FIN/RST; no congestion control, no SACK, no window scaling, no IP fragmentation. Local addresses are delivered through a loopback path.
**Status:** Adopted. See F-107/F-108 for the bugs found by the adaptive-scheduler experiments.

## D-115 — Compositor and window manager in the kernel
**Options:** (A) user-space display server with IPC; (B) kernel compositor with shared-memory surfaces.
**Decision:** B for the prototype: fewer moving parts, and input routing/focus are simple. Surfaces are physical frames mapped into both the kernel and the owning process; composition runs in a kernel thread with damage tracking and without disabling interrupts.
**Tradeoffs:** A compositor bug can crash the kernel; moving it to user space is future work.
**Status:** Adopted.

## D-116 — Browser foundation, not a browser engine
**Decision:** `web` (FuhrerWeb) fetches HTTP over the FuhrerOS stack and renders headings, paragraphs, lists and links. No CSS/JS/images/TLS. §28 forbids writing an engine; porting one needs FPU support (D-104), a larger libc, and TLS.
**Status:** Adopted.

## D-117 — Gesture recognizer tested with synthetic input
**Problem:** QEMU exposes no multi-touch touchpad.
**Decision:** The recognizer consumes abstract contact frames; PS/2 mice bypass it. It is exercised by synthetic frames in tests; real hardware (I²C-HID/Precision Touchpad) is future work.
**Status:** Adopted. **UNKNOWN — REQUIRES VERIFICATION** on real hardware.

## D-118 — Desktop is the default boot entry
**Decision:** The first Limine entry boots the desktop; a text-console entry and the self-test entry remain in the menu.
**Status:** Adopted.

## D-119 — Measure dispatch latency, not only wake-up latency
**Problem:** Wake-up latency (requested wake → running) differed between boots for the same configuration (44 µs vs 975 µs, F-117).
**Context:** Sleepers are woken by the 1000 Hz tick. The tick's phase relative to the probe's deadline depends on the per-boot LAPIC calibration.
**Options:** (A) a one-shot/TSC-deadline timer for precise sleeps; (B) keep the tick and also measure what a policy controls.
**Evidence:** E-105 vs E-108 (same binary, same configuration, two levels).
**Decision:** B. The kernel records each task's runnable→running latency (`SCHED_LAST_DISPATCH`). `schedbench` and `deskstress` report it next to wake latency.
**Reason:** The research question is about scheduling policies, and timer granularity is policy-independent. A is a larger change to the timer subsystem.
**Tradeoffs:** Sleep precision stays at 1 ms.
**Expected impact:** Comparable latency numbers across boots.
**Validation:** E-109..E-120 dispatch columns are consistent across suites (A4 6 µs, sched B3 6–7 µs).
**Status:** Adopted. (A) remains future work.

## D-120 — ACPI power-off without an AML interpreter
**Problem:** Power-off worked only through QEMU's isa-debug-exit port; M18 needs a real power-off and reset.
**Options:** (A) port ACPICA; (B) write an AML interpreter; (C) read `\_S5_` from the DSDT bytes and use the FADT registers.
**Decision:** C: FADT (PM1a/PM1b control, SMI command, reset register, PM timer) plus a byte scan for the `\_S5_` package. Reset tries the FADT reset register, then 0xCF9, then the 8042, then a triple fault.
**Tradeoffs:** Firmware whose `\_S5_` is computed by AML methods will not power off. The fallback is the debug-exit port, then halt.
**Validation:** `scripts/test-power.sh poweroff|reboot` passes without the debug-exit device (QEMU exit code 0). Real hardware: NOT RUN.
**Status:** Adopted.

## D-121 — Input handlers never block; blocking desktop work is deferred
**Problem:** Keyboard/mouse events are processed in interrupt context, but the launcher needs to spawn processes, read directories and switch policies (F-121).
**Options:** (A) a user-space shell process that owns the launcher; (B) a work queue run by the compositor thread.
**Decision:** B (`defer()` in the compositor). Interrupt-side code only updates state and queues work.
**Tradeoffs:** Actions run up to one compositor wake-up later (under a frame).
**Validation:** F-121 regression session (launcher → Settings, Ctrl+Alt+T, `notify`, Super+F).
**Status:** Adopted.

## D-122 — Desktop configuration and theme live in the kernel, apps pull them
**Problem:** Dark/light themes, accents and touchpad options must reach the compositor, the input layer and every app.
**Decision:** One `struct fu_desk_cfg` in the compositor (`DESK_GET_CFG` / `DESK_SET_CFG`). Apps get colours with `DESK_THEME` and are told to redraw with a `WEV_THEME` event. Touchpad options go to the recognizer through `input_set_options`.
**Tradeoffs:** Settings are not saved across reboots yet (no config file). **UNKNOWN - REQUIRES VERIFICATION** whether users expect persistence first.
**Status:** Adopted.

## D-123 — The desktop shows a smoothed system class
**Problem:** The system-level class flickered between IDLE and INTERACTIVE on every 100 ms window (F-120).
**Decision:** Apply the task-level hysteresis to the system class (`profiler_stable_class`). The desktop, notifications and Control Center show it. `/proc/adapt` keeps the instantaneous `system_class`, which the transition experiments measured, and adds `stable_class`.
**Validation:** 3 transitions in 39 s of light use (was 173 in 84 s).
**Status:** Adopted.

## D-124 — Calibration fallback: ACPI PM timer
**Problem:** TSC/LAPIC calibration used only PIT channel 2. Some recent machines lack a working 8254 (docs/real-hardware.md).
**Decision:** Bound the PIT wait. If it does not complete, or if `time=pmtimer` is on the command line, calibrate against the ACPI PM timer (3.579545 MHz, port from the FADT). The source is logged.
**Validation:** `FUHRER_CMDLINE_EXTRA=time=pmtimer ./scripts/test.sh` passes every self-test in QEMU. The PM timer gave TSC 2798.4 MHz, the PIT 2792.2 MHz (single boots, 0.22 % apart). Real hardware: NOT RUN.
**Status:** Adopted.
