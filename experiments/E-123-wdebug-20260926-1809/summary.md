# E-123 — suite `wdebug`

- Date 2026-09-26T18:09:34Z, commit `fceac52-dirty`, kernel FuhrerOS 0.1.0 (own kernel)
- Host Linux 6.6.87.2-microsoft-standard-WSL2 x86_64 / AMD Ryzen 5 7520U with Radeon Graphics (Windows 11 -> WSL2 -> QEMU; KVM nested inside WSL2 when accel=kvm)
- QEMU emulator version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.1), accel **kvm**, 4 vCPU offered (FuhrerOS uses 1), 4096 MB
- virtio-blk, FFS0 raw image, host cache=writeback; virtio-net, QEMU user-mode (slirp)

## Micro-benchmarks (NEW_EXPLANATION §35)

| group | benchmark | median | unit | runs | CV% | note |
|---|---|---|---|---|---|---|
| storage | seq_write | 188 | MB/s x100 | 1 | 0 | 32 MiB in 64 KiB writes + sync |
| storage | seq_read | 31,666 | MB/s x100 | 1 | 0 | cold cache, 64 KiB reads |
| storage | rand_read | 6,720 | ops/s | 1 | 0 | cold cache, 4 KiB |
| storage | rand_write | 853 | ops/s | 1 | 0 | 4 KiB + final sync |
| storage | small_file_create | 256 | files/s | 1 | 0 | 500 x 4 KiB create+write+close, then sync |
| storage | small_file_delete | 11,472 | files/s | 1 | 0 | 500 unlinks, then sync |

## Threats to validity

- Nested virtualisation (Windows → WSL2 → KVM): absolute times are not bare-metal times; comparisons are between policies within one boot.
- FuhrerOS schedules on one CPU (the BSP); extra vCPUs offered by QEMU are idle.
- Sleepers are woken by the 1000 Hz tick, so *wake* latency includes up to 1 ms of timer granularity whose phase depends on the per-boot LAPIC calibration (F-117): compare wake latency only within one experiment. *Dispatch* latency (runnable → running) does not have this problem.
- Small repetition counts; see CV%.
