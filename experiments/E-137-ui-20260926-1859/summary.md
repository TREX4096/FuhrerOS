# E-137 — suite `ui`

- Date 2026-09-26T18:59:08Z, commit `a0ebe22-dirty`, kernel FuhrerOS 0.1.0 (own kernel)
- Host Linux 6.6.87.2-microsoft-standard-WSL2 x86_64 / AMD Ryzen 5 7520U with Radeon Graphics (Windows 11 -> WSL2 -> QEMU; KVM nested inside WSL2 when accel=kvm)
- QEMU emulator version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.1), accel **kvm**, 4 vCPU offered (FuhrerOS uses 1), 4096 MB
- virtio-blk, FFS0 raw image, host cache=writeback; virtio-net, QEMU user-mode (slirp)

## Micro-benchmarks (NEW_EXPLANATION §35)

| group | benchmark | median | unit | runs | CV% | note |
|---|---|---|---|---|---|---|
| desktop | window_create | 1,924 | us | 1 | 0 | open 400x300 + fill + present, avg of 20 |
| desktop | launch_terminal | 5 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 3 ms |
| desktop | launch_files | 3 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 1 ms |
| desktop | launch_editor | 2 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 1 ms |
| desktop | launch_browser | 2 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 1 ms |
| desktop | launch_settings | 2 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 1 ms |
| desktop | launch_control_center | 2 | ms | 1 | 0 | spawn -> window exists; mean of 3, best 1 ms |
| desktop | workspace_switch | 11,994 | us | 1 | 0 | switch request -> composed frame, avg of 10 |
| desktop | workspace_slide_frames | 14 | frames | 1 | 0 | frames composed during one 220 ms slide |
| desktop | animation_compose_max | 3,958 | us | 1 | 0 | worst frame while animating (budget 16,667 us at 60 fps) |
| desktop | compose_avg | 1,526 | us | 1 | 0 | average full composition, whole run |
| desktop | compose_max | 9,405 | us | 1 | 0 | worst composition, whole run |
| desktop | terminal_keystroke_latency | NOT RUN | | | | needs injected keyboard input; not measurable from inside the guest yet |

## Threats to validity

- Nested virtualisation (Windows → WSL2 → KVM): absolute times are not bare-metal times; comparisons are between policies within one boot.
- FuhrerOS schedules on one CPU (the BSP); extra vCPUs offered by QEMU are idle.
- Sleepers are woken by the 1000 Hz tick, so *wake* latency includes up to 1 ms of timer granularity whose phase depends on the per-boot LAPIC calibration (F-117): compare wake latency only within one experiment. *Dispatch* latency (runnable → running) does not have this problem.
- Small repetition counts; see CV%.
