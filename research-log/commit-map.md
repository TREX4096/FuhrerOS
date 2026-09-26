# Commit hash map

On 2026-09-26 the messages of the then-unpushed commits were rewritten (a co-author trailer was removed; the code in every commit is unchanged). Experiment configs and records written before that quote the **old** hashes. This table maps them to the commits in the history.

| old | new | subject |
|---|---|---|
| `d82badf` | `eca1c6c` | m0: freestanding x86_64-elf toolchain, Limine boot, build system |
| `6e4247d` | `4653760` | m1-m3: kernel entry, GDT/IDT/exceptions, APIC timer, PMM/VMM/heap, self-tests |
| `aa54d11` | `360fcd6` | m4: kernel threads, context switching, modular scheduler (RR, priority, low-latency, adaptive) and workload profiler |
| `121a588` | `843fd4d` | m5-m8: processes, syscalls, IPC, VFS, FFS0, virtio-blk, buffer cache, libfu, shell and utilities |
| `b09f5ac` | `54a8d72` | m9: virtio-net, Ethernet/ARP/IPv4/ICMP/UDP/DHCP, TCP and sockets |
| `3facef8` | `f2c2635` | m10-m12: compositor, window manager, desktop shell, GUI apps, PS/2 input and gesture recognizer |
| `1254b4b` | `9dcc709` | m13-m16: research harness, docs, fixes found by experiments (E-101..E-104 pre-fix runs) |
| `32d80f9` | `d731871` | m3: heap.small_latency self-test (fix variable clash) |
| `ba10d0c` | `eb022f5` | m16: F-114 record, EXPLANATION.md, desktop screenshot, analyze.py number formatting |
| `d5fc346` | `45d470f` | m13-m14: fix adaptive aging starvation (F-115) and read-ahead self-eviction (F-116); E-105..E-108 runs that exposed them |
| `5e3f4cf` | `2cfedcf` | m16: measure dispatch latency (SCHED_LAST_DISPATCH); F-117 wake latency depends on per-boot tick phase |
| `e70e29b` | `9d61a33` | m7/m14: wake from IRQ on idle CPU switches immediately (F-118); iobench overflow; E-109..E-112 |
| `689bf6c` | `c7f937c` | m16: experiment.sh: results of earlier suites do not mark the tree dirty |
| `e942a12` | `96b4fa6` | m14/m16: evict-behind only for data blocks (F-119); ablation A5 (adaptive cache) and LRU elsewhere; E-113..E-116; E-101..E-112 records |
| `9610454` | `c55ed35` | m16: final campaign E-117..E-120, docs/experiments.md, experiment records E-101..E-120 |
| `62c09f4` | `9a0b54c` | m12: 4-finger up/down, 3/4-finger taps, touchpad options (tap-to-click, natural scroll, speeds, disable-while-typing) with self-tests |
| `c9e64eb` | `895573a` | m18: ACPI S5 poweroff (FADT + \_S5_ from the DSDT) and reset (FADT reset register, 0xCF9, 8042); reboot command |
| `bb59e8e` | `73a2c42` | m11: desktop redesign per UI_SUGGESTION.md - logo, top bar, themes/accents, launcher + command palette, floating/tiling/focus, Alt+Tab, notifications, configurable gestures, new Control Center and Settings; F-120, F-121; UI-D-001..006 |
| `f8d20f0` | `53afe79` | m18: scripts/test-power.sh - poweroff/reboot verified without isa-debug-exit |
