# FuhrerOS VM Guide

FuhrerOS is developed and evaluated **only in QEMU** (INSTRUCTION Principle 1).
Nothing in this repository writes to host partitions, the host bootloader or
the host filesystem outside the artifact directory.

## Host requirements

| | Linux host | Windows host (tested: Windows 11 + WSL2 Ubuntu 26.04) |
|---|---|---|
| Build | Docker | Docker Desktop (WSL integration) |
| Run | `qemu-system-x86_64`, `qemu-img` | QEMU inside WSL (`apt install qemu-system-x86 qemu-utils`) |
| Tests | `ssh`, `nc`, `python3` | same, inside WSL |
| Acceleration | KVM if `/dev/kvm` is accessible, else TCG | nested KVM inside WSL2 (user in `kvm` group), else TCG |

The scripts detect acceleration and print `Acceleration: KVM` or
`Acceleration: TCG`. Force one with `FUHRER_ACCEL=tcg`. If `/dev/kvm` exists
but is not accessible, `sudo usermod -aG kvm $USER` (then open a new shell).

From Windows, every script can be called from Git Bash (it re-executes itself
inside WSL) or via `fuhreros.cmd build|run|test|benchmark|reset|ssh`.

## VM configurations (`vm/configs/*.conf`)

| config | vCPU | RAM | disk | SSH port | use |
|---|---|---|---|---|---|
| `dev` (default) | 2 | 2 GB | 20 GB | 2222 | development |
| `bench` | 4 | 4 GB | 40 GB | 2223 | all benchmarks |
| `stress` | 4 | 6 GB | 50 GB | 2224 | stress; never all 8 GB of the host |

Select with `FUHRER_VM=bench`. Each config has its own disk, so they do not
interfere. All configs use virtio-blk, virtio-net (QEMU user networking,
SSH forwarded to 127.0.0.1 only) and virtio-rng on a q35 machine.

## Commands

```bash
./scripts/build.sh                        # base image (≈3 min first time)
FUHRER_PROFILE=desktop ./scripts/build.sh # + Xorg/Openbox/Firefox ESR
./scripts/run.sh                          # boot; serial console here (Ctrl-A X quits)
./scripts/run.sh --headless               # boot in background
./scripts/ssh.sh [cmd]                    # root shell over SSH
./scripts/reset.sh [--stop]               # destroy + recreate the VM disk
./scripts/debug.sh [--wait]               # verbose kernel, gdb stub :1234
./scripts/test.sh                         # acceptance test (fresh VM)
./scripts/benchmark.sh --suite main       # experiments (bench VM)
./scripts/screenshot.sh out.png           # PNG of a headless VM's screen
make | make run | make test | make benchmark | make clean
```

Desktop: `FUHRER_PROFILE=desktop ./scripts/run.sh` opens a QEMU window (WSLg
on Windows). tty1 auto-logs in user `fuhrer` and starts X (Openbox, tint2
panel, dashboard terminal; right-click the desktop for Firefox, NetSurf,
files, terminal).

Kernel parameters after `--`:

| parameter | effect |
|---|---|
| `fuhrer.disable=1` | fuhrerd not started → baseline B0 (plain Linux) |
| `fuhrer.mode=observe` | profile only (A2) |
| `fuhrer.mode=off` | daemon idle (A1) |
| `fuhrer.mode=static:batched` | fixed policy |

## Disks and artifacts

```text
$FU_OUT/                       ($HOME/.cache/fuhreros/out when the repo is on /mnt/*)
├── fuhreros-base.qcow2        immutable base image (20 GB virtual, ≈500 MB)
├── vmlinuz-base, initramfs-base
├── vm-dev-base.qcow2          copy-on-write overlay = the VM's writable disk
├── vm-dev-base.serial.log     serial console of headless runs
├── build-info-base.json       kernel, Alpine release, commit, package count
├── packages-base.txt          exact package versions
└── ssh/id_ed25519             key authorised for root and fuhrer in the guest
```

`reset.sh` deletes the overlay; the next boot starts from the pristine base.
The acceptance test checks the base image's SHA-256 is unchanged.

## Guest accounts (development only — D-013)

| user | password | notes |
|---|---|---|
| root | fuhrer | serial console auto-login; SSH key login |
| fuhrer | fuhrer | wheel (sudo); desktop session user |

## Acceptance (INSTRUCTION §17) — `./scripts/test.sh`

```text
[✓] VM boots                     vm-boot
[✓] Kernel/userspace starts      guest-selftest: kernel, userspace
[✓] Terminal works               serial console + SSH shell
[✓] Network interface appears    net-iface, net-ipv4, virtio-net
[✓] Virtual disk works           root-disk, disk-write (sha256 round trip)
[✓] SSH/console access works     sshd + every check runs over SSH
[✓] VM can be destroyed/recreated vm-destroy-recreate
[✓] No host disk is modified     base-image-immutable
```

## Troubleshooting

- *"docker daemon not reachable"*: start Docker Desktop.
- *Boot timeout*: see `vm-*.serial.log`; `./scripts/debug.sh` for verbose boot.
- *Slow under TCG*: expected (5–20× slower); benchmark numbers from TCG and
  KVM runs must never be compared.
- *Port in use*: another VM of the same config is running (`reset.sh --stop`).
