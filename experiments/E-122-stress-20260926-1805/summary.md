# E-122 — suite `stress`

- Date 2026-09-26T18:05:44Z, commit `fceac52-dirty`, kernel FuhrerOS 0.1.0 (own kernel)
- Host Linux 6.6.87.2-microsoft-standard-WSL2 x86_64 / AMD Ryzen 5 7520U with Radeon Graphics (Windows 11 -> WSL2 -> QEMU; KVM nested inside WSL2 when accel=kvm)
- QEMU emulator version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.1), accel **kvm**, 4 vCPU offered (FuhrerOS uses 1), 4096 MB
- virtio-blk, FFS0 raw image, host cache=writeback; virtio-net, QEMU user-mode (slirp)

## Desktop stress (M17; median of 3 runs; CV%)

| metric | B0 round-robin | B1 priority | B2 low-latency | B3 adaptive |
|---|---|---|---|---|
| probe dispatch p50 (us) | 19,189 (0%) | 19,193 (0%) | 10 (41%) | 49,183 (0%) |
| probe dispatch p99 (us) | 21,295 (4%) | 21,731 (8%) | 59 (65%) | 59,110 (59%) |
| probe wake p99 (us) | 22,096 (3%) | 23,289 (9%) | 3,991 (22%) | 59,907 (59%) |
| build-proxy jobs/s (x100) | 67,825 (2%) | 67,938 (2%) | 48,748 (8%) | 68,143 (2%) |
| file copy MB/s (x100) | 21 (0%) | 21 (3%) | 118 (7%) | 6 (38%) |
| HTTP requests/s (x100) | 9 (87%) | 0 (173%) | 19 (101%) | 0 (173%) |
| TCP stream MB/s (x100) | 208 (3%) | 214 (2%) | 3,653 (2%) | 209 (21%) |
| ctx switches/s | 336 (5%) | 365 (4%) | 2,854 (1%) | 209 (7%) |
| system class (mid-run) | CPU_BOUND | CPU_BOUND | IO_BOUND | CPU_BOUND |

Media playback: NOT RUN (no audio/video path). The build workload is a CPU-bound proxy: no compiler has been ported.

## Threats to validity

- Nested virtualisation (Windows → WSL2 → KVM): absolute times are not bare-metal times; comparisons are between policies within one boot.
- FuhrerOS schedules on one CPU (the BSP); extra vCPUs offered by QEMU are idle.
- Sleepers are woken by the 1000 Hz tick, so *wake* latency includes up to 1 ms of timer granularity whose phase depends on the per-boot LAPIC calibration (F-117): compare wake latency only within one experiment. *Dispatch* latency (runnable → running) does not have this problem.
- Small repetition counts; see CV%.
