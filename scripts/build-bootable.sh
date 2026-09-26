#!/bin/bash
# Produce a self-booting raw disk image (GPT, GRUB for BIOS and UEFI) from a
# built FuhrerOS image, and optionally boot-test it in QEMU without -kernel.
#
#   ./scripts/build-bootable.sh [--kernel virt|lts] [--test]
#
# --kernel lts installs the generic Linux LTS kernel + common firmware so the
# image can be written to a USB stick for the bare-metal milestone:
#   dd if=$FU_OUT/fuhreros-base-lts-bootable.img of=/dev/sdX bs=4M conv=fsync
# (That step is deliberately manual: it overwrites a whole device.)
. "$(dirname "$0")/lib.sh"

KERNEL=virt TEST=0
while [ $# -gt 0 ]; do
	case $1 in
	--kernel) KERNEL=$2; shift ;;
	--test) TEST=1 ;;
	*) fu_die "unknown option $1" ;;
	esac
	shift
done
[ -f "$FU_BASE_IMG" ] || fu_die "run ./scripts/build.sh first"
docker image inspect fuhreros-builder >/dev/null 2>&1 || fu_die "builder image missing; run ./scripts/build.sh"

docker run --rm --privileged -v /dev:/dev \
	-e PROFILE="$FU_PROFILE" -e KERNEL="$KERNEL" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
	-v "$FU_OUT:/out" -v "$FU_REPO/vm/image/mkbootable.sh:/mkbootable.sh:ro" \
	fuhreros-builder /bin/bash /mkbootable.sh
IMG=$FU_OUT/fuhreros-$FU_PROFILE-$KERNEL-bootable.img
echo "BOOTABLE: $IMG"

[ $TEST = 1 ] || exit 0
fu_detect_accel
boot_test() { # firmware-args... ; boots the image via its own bootloader
	local log=$FU_OUT/bootable-test.log
	: >"$log"
	timeout 240 qemu-system-x86_64 -machine "q35,accel=$FU_ACCEL" -cpu "$FU_CPU" -smp 2 -m 2048 \
		"$@" -drive "file=$IMG,if=virtio,format=raw,snapshot=on" \
		-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
		-display none -serial "file:$log" -no-reboot &
	local pid=$! t=0
	until grep -q FUHREROS-BOOT-COMPLETE "$log" 2>/dev/null; do
		sleep 2; t=$((t + 2))
		kill -0 $pid 2>/dev/null || break
		[ $t -lt 240 ] || break
	done
	kill $pid 2>/dev/null; wait $pid 2>/dev/null || true
	grep -q FUHREROS-BOOT-COMPLETE "$log"
}
if boot_test; then echo "BIOS boot: PASS"; else echo "BIOS boot: FAIL (see $FU_OUT/bootable-test.log)"; fi
OVMF=$(ls /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/qemu/OVMF.fd 2>/dev/null | head -1 || true)
if [ -n "$OVMF" ]; then
	if boot_test -bios "$OVMF"; then echo "UEFI boot: PASS"; else echo "UEFI boot: FAIL"; fi
else
	echo "UEFI boot: SKIPPED (no OVMF firmware on host; apt install ovmf)"
fi
