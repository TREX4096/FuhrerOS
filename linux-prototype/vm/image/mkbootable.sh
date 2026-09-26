#!/bin/bash
# Turn a FuhrerOS qcow2 image into a self-booting raw disk (GPT; GRUB for
# both BIOS and UEFI). Runs in a *privileged* builder container because it
# needs loop devices; it only ever touches files under /out.
#
# Environment:
#   PROFILE  base | desktop     (source: /out/fuhreros-$PROFILE.qcow2)
#   KERNEL   virt | lts         (lts adds the generic kernel + firmware for real hardware)
#   SIZE_GB  output disk size   (default 4 for base, 8 for desktop)
#   HOST_UID/HOST_GID
set -euo pipefail
PROFILE=${PROFILE:-base}
KERNEL=${KERNEL:-virt}
SIZE_GB=${SIZE_GB:-$([ "$PROFILE" = desktop ] && echo 8 || echo 4)}
SRC=/out/fuhreros-$PROFILE.qcow2
DST=/out/fuhreros-$PROFILE-$KERNEL-bootable.img
log() { printf '[mkbootable] %s\n' "$*"; }

[ -f "$SRC" ] || { echo "missing $SRC (run build.sh first)" >&2; exit 1; }
apk add --no-cache -q grub grub-bios grub-efi sfdisk dosfstools rsync util-linux >/dev/null

LOOPS=()
cleanup() {
	set +e
	umount -R /mnt/dst 2>/dev/null; umount /mnt/src 2>/dev/null
	for l in "${LOOPS[@]}"; do losetup -d "$l"; done
}
trap cleanup EXIT

log "expanding $SRC"
qemu-img convert -O raw "$SRC" /tmp/rootfs.raw
SRC_LOOP=$(losetup -f --show -r /tmp/rootfs.raw); LOOPS+=("$SRC_LOOP")
mkdir -p /mnt/src /mnt/dst
mount -o ro "$SRC_LOOP" /mnt/src

log "partitioning ${SIZE_GB}G GPT disk"
rm -f "$DST.tmp"
truncate -s "${SIZE_GB}G" "$DST.tmp"
sfdisk -q "$DST.tmp" <<'EOF'
label: gpt
size=1MiB, type=21686148-6449-6E6F-744E-656564454649, name=bios
size=64MiB, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name=esp
type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name=fuhreros
EOF
DLOOP=$(losetup -f --show -P "$DST.tmp"); LOOPS+=("$DLOOP")
# Partition nodes are not always created inside containers.
for n in 1 2 3; do
	[ -b "${DLOOP}p$n" ] || mknod "${DLOOP}p$n" b "$(cat /sys/block/${DLOOP#/dev/}/${DLOOP#/dev/}p$n/dev | tr : ' ')"
done
mkfs.vfat -F 32 -n FUHREFI "${DLOOP}p2" >/dev/null
mkfs.ext4 -q -L fuhreros "${DLOOP}p3"
mount "${DLOOP}p3" /mnt/dst
mkdir -p /mnt/dst/boot/efi
mount "${DLOOP}p2" /mnt/dst/boot/efi

log "copying root filesystem"
rsync -aHAX /mnt/src/ /mnt/dst/ --exclude=/boot/efi
echo 'LABEL=FUHREFI	/boot/efi	vfat	defaults,nofail	0 2' >>/mnt/dst/etc/fstab

VMLINUZ=vmlinuz-virt INITRD=initramfs-virt
if [ "$KERNEL" = lts ]; then
	log "installing linux-lts + firmware for real hardware"
	cp /etc/resolv.conf /mnt/dst/etc/resolv.conf
	apk add --root /mnt/dst --no-progress -q linux-lts linux-firmware-none \
		linux-firmware-amdgpu linux-firmware-i915 linux-firmware-intel linux-firmware-rtl_nic \
		linux-firmware-other >/dev/null
	KV=$(ls /mnt/dst/lib/modules | grep -- -lts | head -1)
	mkinitfs -b /mnt/dst -c /mnt/dst/etc/mkinitfs/mkinitfs.conf -o /mnt/dst/boot/initramfs-lts "$KV" >/dev/null
	VMLINUZ=vmlinuz-lts INITRD=initramfs-lts
fi

UUID=$(blkid -s UUID -o value "${DLOOP}p3")
log "installing GRUB (BIOS + UEFI), root UUID=$UUID"
grub-install --target=i386-pc --boot-directory=/mnt/dst/boot "$DLOOP" >/dev/null 2>&1
grub-install --target=x86_64-efi --efi-directory=/mnt/dst/boot/efi \
	--boot-directory=/mnt/dst/boot --removable --no-nvram >/dev/null 2>&1
cat >/mnt/dst/boot/grub/grub.cfg <<EOF
set timeout=3
set default=0
insmod part_gpt
insmod ext2
search --no-floppy --fs-uuid --set=root $UUID
menuentry "FuhrerOS ($KERNEL kernel)" {
	linux /boot/$VMLINUZ root=UUID=$UUID rootfstype=ext4 modules=ext4 rw console=tty0 console=ttyS0,115200
	initrd /boot/$INITRD
}
menuentry "FuhrerOS — plain Linux baseline (fuhrer.disable=1)" {
	linux /boot/$VMLINUZ root=UUID=$UUID rootfstype=ext4 modules=ext4 rw console=tty0 console=ttyS0,115200 fuhrer.disable=1
	initrd /boot/$INITRD
}
EOF
sync
umount -R /mnt/dst
mv "$DST.tmp" "$DST"
[ -n "${HOST_UID:-}" ] && chown "$HOST_UID:${HOST_GID:-$HOST_UID}" "$DST"
log "done: $DST"
