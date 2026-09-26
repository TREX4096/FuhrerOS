#!/bin/bash
# Write the FuhrerOS hybrid ISO (BIOS + UEFI) to a USB stick for a real-
# hardware boot (NEW_EXPLANATION M18: never the internal disk).
#
#   ./scripts/make-usb.sh /dev/sdX
#
# Safety checks, all of which must pass:
#   - the target is a whole block device (not a partition),
#   - the kernel reports it as removable (RM=1) or attached over USB,
#   - none of its partitions is mounted,
#   - you type the device name again to confirm.
# In WSL the stick is only visible after `usbipd attach --wsl` on Windows;
# alternatively use Rufus in "DD image" mode with $B/fuhreros.iso.
. "$(dirname "$0")/common.sh"
DEV=${1:?usage: make-usb.sh /dev/sdX}
ISO=${FUHRER_ISO:-$B/fuhreros.iso}
[ -f "$ISO" ] || die "no ISO at $ISO (run ./scripts/build.sh)"
[ -b "$DEV" ] || die "$DEV is not a block device"
[ "$(lsblk -dno TYPE "$DEV")" = disk ] || die "$DEV is not a whole disk"
RM=$(lsblk -dno RM "$DEV" | tr -d ' ')
TRAN=$(lsblk -dno TRAN "$DEV" | tr -d ' ')
[ "$RM" = 1 ] || [ "$TRAN" = usb ] || die "$DEV is neither removable nor USB (RM=$RM TRAN=$TRAN) - refusing"
if lsblk -no MOUNTPOINT "$DEV" | grep -q .; then
	die "$DEV has mounted partitions - unmount them first"
fi
SIZE=$(lsblk -dno SIZE "$DEV")
MODEL=$(lsblk -dno MODEL "$DEV")
ISOSZ=$(stat -c %s "$ISO")
echo "Target : $DEV  ($MODEL, $SIZE, RM=$RM, TRAN=$TRAN)"
echo "Image  : $ISO  ($ISOSZ bytes)"
echo "EVERYTHING on $DEV will be overwritten."
read -r -p "Type the device name ($DEV) to continue: " ok
[ "$ok" = "$DEV" ] || die "not confirmed"
sudo dd if="$ISO" of="$DEV" bs=4M conv=fsync status=progress
sync
# read back and compare the written image
sudo cmp -n "$ISOSZ" "$ISO" "$DEV" && log "USB: PASS (written and verified)" || die "USB: verify FAILED"
