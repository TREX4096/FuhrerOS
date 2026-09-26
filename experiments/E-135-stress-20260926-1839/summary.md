# E-135 — suite `stress`

- Date 2026-09-26T18:39:49Z, commit `389c7f5`, kernel FuhrerOS 0.1.0 (own kernel)
- Host Linux 6.6.87.2-microsoft-standard-WSL2 x86_64 / AMD Ryzen 5 7520U with Radeon Graphics (Windows 11 -> WSL2 -> QEMU; KVM nested inside WSL2 when accel=kvm)
- QEMU emulator version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.1), accel **kvm**, 4 vCPU offered (FuhrerOS uses 1), 4096 MB
- virtio-blk, FFS0 raw image, host cache=writeback; virtio-net, QEMU user-mode (slirp)

## Desktop stress (M17; median of 3 runs; CV%)

| metric | B0 round-robin | B1 priority | B2 low-latency | B3 adaptive |
|---|---|---|---|---|
| probe dispatch p50 (us) | 19,528 (0%) | 19,447 (0%) | 9 (17%) | 14 (18%) |
| probe dispatch p99 (us) | 28,355 (23%) | 32,736 (15%) | 245 (48%) | 3,019 (50%) |
| probe wake p99 (us) | 30,285 (25%) | 33,519 (14%) | 19,486 (36%) | 6,228 (53%) |
| build-proxy jobs/s (x100) | 55,441 (13%) | 55,726 (3%) | 19,665 (12%) | 4,209 (8%) |
| file copy MB/s (x100) | 20 (8%) | 19 (8%) | 644 (24%) | 551 (17%) |
| HTTP requests/s (x100) | 9 (0%) | 9 (0%) | 209 (28%) | 19 (106%) |
| TCP stream MB/s (x100) | 206 (4%) | 204 (1%) | 3,870 (18%) | 9,924 (14%) |
| ctx switches/s | 332 (7%) | 324 (2%) | 5,095 (20%) | 8,340 (14%) |
| system class (mid-run) | CPU_BOUND | CPU_BOUND | IO_BOUND | IO_BOUND |

Threads of the benchmark at mid-run (`tid:priority:class`, first run of each policy):

- B0 round-robin: `8:0:IDLE,10:0:INTERACTIVE,11:0:INTERACTIVE,12:0:CPU_BOUND,13:0:CPU_BOUND,14:0:INTERACTIVE,15:0:IDLE,16:0:IO_BOUND`
- B1 priority: `17:16:IDLE,19:16:INTERACTIVE,20:16:INTERACTIVE,21:16:CPU_BOUND,22:16:CPU_BOUND,23:16:INTERACTIVE,24:16:IDLE,25:16:IO_BOUND`
- B2 low-latency: `26:0:IDLE,28:0:IO_BOUND,29:0:INTERACTIVE,30:0:CPU_BOUND,31:0:CPU_BOUND,32:0:IO_BOUND,33:0:IDLE,34:0:IO_BOUND`
- B3 adaptive: `35:4:IDLE,37:8:IO_BOUND,38:4:INTERACTIVE,39:12:MIXED,40:12:MIXED,41:8:IO_BOUND,42:12:IDLE,43:8:IO_BOUND`

Media playback: NOT RUN (no audio/video path). The build workload is a CPU-bound proxy: no compiler has been ported.

## Threats to validity

- Nested virtualisation (Windows → WSL2 → KVM): absolute times are not bare-metal times; comparisons are between policies within one boot.
- FuhrerOS schedules on one CPU (the BSP); extra vCPUs offered by QEMU are idle.
- Sleepers are woken by the 1000 Hz tick, so *wake* latency includes up to 1 ms of timer granularity whose phase depends on the per-boot LAPIC calibration (F-117): compare wake latency only within one experiment. *Dispatch* latency (runnable → running) does not have this problem.
- Small repetition counts; see CV%.
