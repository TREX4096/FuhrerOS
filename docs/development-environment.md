# Development environment

```text
Windows 11 ─► WSL 2 (Ubuntu 26.04) ─► x86_64-elf toolchain, QEMU, GDB ─► FuhrerOS VM
```

WSL is only the development host; FuhrerOS runs in QEMU with its own kernel.

## One-time setup (inside WSL)
```bash
sudo apt install build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev \
                 texinfo xorriso mtools qemu-system-x86 gdb python3
./toolchain/build-toolchain.sh   # binutils 2.45 + GCC 15.2 -> ~/opt/cross (x86_64-elf)
./toolchain/get-limine.sh        # Limine 11.x binary release -> ~/src/limine
```
For KVM inside WSL: `sudo usermod -aG kvm $USER` (then a new shell).

## Daily use (Git Bash or WSL; scripts re-run themselves inside WSL)
```bash
./scripts/build.sh          # kernel, user space, FFS0 disk, ISOs  -> BUILD: PASS
./scripts/run.sh            # boot the desktop in a QEMU window (serial on this terminal)
./scripts/test.sh           # self-test boot: kernel + user-space + network tests
./scripts/run-debug.sh      # QEMU halted + GDB attached with symbols, breakpoint at kmain
./scripts/reset.sh          # discard the persistent VM disk
./scripts/experiment.sh S   # research suite S in {sched, cache, transition, ablation}
./scripts/screenshot.sh P   # headless boot + PNG screenshots via the QEMU monitor
```
Build outputs go to `~/.cache/fuhreros/build` when the checkout is on a
Windows drive (D-106).

## Acceleration
Scripts print `Acceleration: KVM` or `Acceleration: TCG` and never fall back
silently. `FUHRER_ACCEL=tcg` forces TCG (the debug script uses TCG for
reliable breakpoints). WHPX would require a Windows-native QEMU build, which
is not installed on this machine (**NOT RUN**).

## VM configurations
Development: `FUHRER_VCPUS=2 FUHRER_MEM=2048` (defaults of run.sh/test.sh).
Benchmark: 4 vCPU, 4096 MB (experiment.sh). The FFS0 disk is 20 GiB
(sparse). Network: virtio-net on QEMU user networking with host forwards
127.0.0.1:8080 → 80 and 127.0.0.1:7777 → 7.
