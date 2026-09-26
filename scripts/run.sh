#!/bin/bash
# Boot FuhrerOS in QEMU. Serial console on this terminal; framebuffer in a
# window (FUHRER_DISPLAY=none for headless). Ctrl-A X quits.
#   ./scripts/run.sh [extra qemu args]
. "$(dirname "$0")/common.sh"
ISO=${FUHRER_ISO:-$B/fuhreros.iso}
DISK=${FUHRER_DISK:-$B/vm-disk.img}
[ -f "$ISO" ] || die "missing $ISO — run ./scripts/build.sh"
# Persistent VM disk: recreated from the pristine image when that is newer.
if [ -z "${FUHRER_DISK:-}" ] && { [ ! -f "$DISK" ] || [ "$B/disk.img" -nt "$DISK" ]; }; then
	log "creating VM disk from the pristine FFS0 image"
	cp --sparse=always "$B/disk.img" "$DISK"
fi
detect_accel
qemu_base "$ISO" "$DISK"
QEMU+=(-serial mon:stdio -display "${FUHRER_DISPLAY:-gtk}" -vga std)
log "VM: $VCPUS vCPU, $MEM MB, virtio-blk $(basename "$DISK"), virtio-net"
exec "${QEMU[@]}" "$@"
