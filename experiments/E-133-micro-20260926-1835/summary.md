# E-133 — suite `micro`

- Date 2026-09-26T18:35:31Z, commit `4768a1f`, kernel FuhrerOS 0.1.0 (own kernel)
- Host Linux 6.6.87.2-microsoft-standard-WSL2 x86_64 / AMD Ryzen 5 7520U with Radeon Graphics (Windows 11 -> WSL2 -> QEMU; KVM nested inside WSL2 when accel=kvm)
- QEMU emulator version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.1), accel **kvm**, 4 vCPU offered (FuhrerOS uses 1), 4096 MB
- virtio-blk, FFS0 raw image, host cache=writeback; virtio-net, QEMU user-mode (slirp)

## Micro-benchmarks (NEW_EXPLANATION §35)

| group | benchmark | median | unit | runs | CV% | note |
|---|---|---|---|---|---|---|
| cpu | prime | 62 | ms | 3 | 6 | 25997 primes below 300000 (trial division) |
| cpu | matrix | 10 | ms | 3 | 19 | 4 x 160x160 int32 multiply, checksum 4988 |
| cpu | compilation | NOT RUN | | | | no compiler has been ported to FuhrerOS |
| mem | alloc_free | 19 | ns/pair | 3 | 23 | malloc 16..4111 B + free, 64 live objects |
| mem | copy | 4,579 | MB/s | 3 | 8 | memcpy 4 MiB x 32 (libfu memcpy, no SSE) |
| mem | working_set_16K | 12 | ns/10 access | 3 | 5 | dependent random loads (end 1990) |
| mem | working_set_256K | 33 | ns/10 access | 3 | 5 | dependent random loads (end 32867) |
| mem | working_set_4096K | 690 | ns/10 access | 3 | 24 | dependent random loads (end 611571) |
| mem | working_set_32768K | 1,540 | ns/10 access | 3 | 3 | dependent random loads (end 7222772) |
| storage | seq_write | 2,521 | MB/s x100 | 3 | 1 | 32 MiB in 64 KiB writes + sync |
| storage | seq_read | 37,034 | MB/s x100 | 3 | 27 | cold cache, 64 KiB reads |
| storage | rand_read | 5,739 | ops/s | 3 | 21 | cold cache, 4 KiB |
| storage | rand_write | 4,487 | ops/s | 3 | 4 | 4 KiB + final sync |
| storage | small_file_create | 1,402 | files/s | 3 | 36 | 500 x 4 KiB create+write+close, then sync |
| storage | small_file_delete | 12,835 | files/s | 3 | 9 | 500 unlinks, then sync |
| net | tcp_throughput | 15,353 | MB/s x100 | 3 | 5 | 32 MiB to the guest's own IP (loopback path of the own stack) |
| net | tcp_rtt_p50 | 2 | us | 3 | 0 | 1-byte ping-pong x 2000, p99 3 us |
| net | udp_delivered_rate | 87,913 | packets/s | 3 | 13 | 64 B datagrams, <=16 in flight: 20000 sent, 20000 delivered |
| desktop | window_create | 1,666 | us | 1 | 0 | open 400x300 + fill + present, avg of 20 |
| desktop | launch_terminal | 4 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 1 ms |
| desktop | launch_files | 3 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 3 ms |
| desktop | launch_editor | 3 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 2 ms |
| desktop | launch_browser | 3 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 2 ms |
| desktop | launch_settings | 3 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 2 ms |
| desktop | launch_control_center | 3 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 3 ms |
| desktop | workspace_switch | 1,314 | us | 1 | 0 | switch request -> composed frame, avg of 10 |
| desktop | compose_avg | 1,716 | us | 1 | 0 | average full composition, whole run |
| desktop | compose_max | 7,894 | us | 1 | 0 | worst composition, whole run |
| desktop | terminal_keystroke_latency | NOT RUN | | | | needs injected keyboard input; not measurable from inside the guest yet |

## Threats to validity

- Nested virtualisation (Windows → WSL2 → KVM): absolute times are not bare-metal times; comparisons are between policies within one boot.
- FuhrerOS schedules on one CPU (the BSP); extra vCPUs offered by QEMU are idle.
- Sleepers are woken by the 1000 Hz tick, so *wake* latency includes up to 1 ms of timer granularity whose phase depends on the per-boot LAPIC calibration (F-117): compare wake latency only within one experiment. *Dispatch* latency (runnable → running) does not have this problem.
- Small repetition counts; see CV%.
