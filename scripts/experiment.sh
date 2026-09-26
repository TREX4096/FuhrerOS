#!/bin/bash
# Run a benchmark suite inside FuhrerOS and record the results.
#   ./scripts/experiment.sh SUITE     (sched | cache | transition | ablation)
# Builds a one-off ISO whose kernel command line is "bench=SUITE", boots it
# headless on a fresh copy of the disk image, waits for the guest to power
# off, and writes experiments/E-NNN-SUITE-DATE/ with config.json, the raw
# serial log, results.jsonl and (via analyze.py) summary.md + SVG graphs.
. "$(dirname "$0")/common.sh"
SUITE=${1:?usage: experiment.sh SUITE}
[ -f "$REPO/rootfs/etc/bench/$SUITE.sh" ] || die "no suite rootfs/etc/bench/$SUITE.sh"
[ -f "$B/disk.img" ] || die "build first (./scripts/build.sh)"
VCPUS=${FUHRER_VCPUS:-4}   # benchmark VM (NEW_EXPLANATION §5)
MEM=${FUHRER_MEM:-4096}

mkdir -p "$REPO/experiments"
N=$(find "$REPO/experiments" -maxdepth 1 -name 'E-1[0-9][0-9]*' | sed -E 's#.*/E-([0-9]+)-.*#\1#' | sort -n | tail -1)
N=$((10#${N:-100} + 1))
EXP=$REPO/experiments/E-$N-$SUITE-$(date +%Y%m%d-%H%M)
mkdir -p "$EXP"
log "experiment $EXP"

# ISO with the suite on the kernel command line
ISODIR=$B/iso-exp
rm -rf "$ISODIR" && cp -r "$B/iso-main" "$ISODIR"
sed -i -E "0,/cmdline:.*/s//cmdline: bench=$SUITE/" "$ISODIR/boot/limine/limine.conf"
sed -i -E 's/^timeout:.*/timeout: 0/' "$ISODIR/boot/limine/limine.conf"
xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
	-boot-info-table -hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
	-efi-boot-part --efi-boot-image --protective-msdos-label "$ISODIR" -o "$B/exp.iso" 2>/dev/null
"$HOME/src/limine/limine" bios-install "$B/exp.iso" >/dev/null 2>&1
cp --sparse=always "$B/disk.img" "$B/disk-exp.img"

detect_accel 2>"$EXP/accel.txt"
REV=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)
[ -n "$(git -C "$REPO" status --porcelain -- . ':!experiments' 2>/dev/null)" ] && REV="$REV-dirty"
cat >"$EXP/config.json" <<EOF
{
  "experiment": "E-$N",
  "suite": "$SUITE",
  "date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "commit": "$REV",
  "kernel": "FuhrerOS $(grep -o 'FUHREROS_VERSION "[^"]*"' "$REPO/kernel/include/kernel.h" | cut -d'"' -f2) (own kernel)",
  "host": "$(uname -srm)",
  "host_cpu": "$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs)",
  "host_note": "Windows 11 -> WSL2 -> QEMU; KVM nested inside WSL2 when accel=kvm",
  "qemu": "$(qemu-system-x86_64 --version | head -1)",
  "accel": "$ACCEL",
  "vcpus": $VCPUS,
  "vcpus_used_by_kernel": 1,
  "memory_mb": $MEM,
  "disk": "virtio-blk, FFS0 raw image, host cache=writeback",
  "network": "virtio-net, QEMU user-mode (slirp)",
  "suite_script": "rootfs/etc/bench/$SUITE.sh"
}
EOF
cp "$REPO/rootfs/etc/bench/$SUITE.sh" "$EXP/suite.sh"
qemu_base "$B/exp.iso" "$B/disk-exp.img"
QEMU+=(-serial "file:$EXP/serial.log" -display none -vga std)
set +e
timeout "${FUHRER_EXP_TIMEOUT:-3600}" "${QEMU[@]}"
rc=$?
set -e
log "guest finished (qemu exit $rc)"
grep -aoE '(SCHEDBENCH|IOBENCH|TRANSITION|TL|FUBENCH|DESKSTRESS) \{.*\}' "$EXP/serial.log" | sed -E 's/^([A-Z]+) /{"kind":"\1","data":/; s/$/}/' >"$EXP/results.jsonl" || true
log "$(wc -l <"$EXP/results.jsonl") result records"
python3 "$REPO/scripts/analyze.py" "$EXP"
