#!/bin/bash
# Assemble the FuhrerOS root filesystem and disk image.
# Runs as root inside the fuhreros-builder container; never touches the host
# except through the /out bind mount.
#
# Environment:
#   PROFILE   base | desktop           (default base)
#   DISK_GB   virtual disk size        (default 20)
#   GIT_REV   commit recorded in build-info
set -euo pipefail

PROFILE=${PROFILE:-base}
DISK_GB=${DISK_GB:-20}
GIT_REV=${GIT_REV:-unknown}
OUT=/out
WORK=/work
ROOT=$WORK/rootfs
MIRROR=${ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine}

log() { printf '[mkimage] %s\n' "$*"; }

PKGS_BASE=(
	alpine-base openrc busybox-openrc busybox-mdev-openrc linux-virt linux-firmware-none mkinitfs kmod
	e2fsprogs e2fsprogs-extra util-linux agetty shadow sudo bash bash-completion coreutils findutils grep sed gawk
	less file procps-ng htop iproute2 iputils ca-certificates curl wget openssh openssh-server
	chrony tzdata
	python3 py3-pip gcc g++ musl-dev make git linux-headers liburing liburing-dev
	vim nano tmux lynx w3m
	fio sysstat strace iperf3
)
PKGS_DESKTOP=(
	eudev udev-init-scripts udev-init-scripts-openrc dbus dbus-x11
	xorg-server xinit xf86-input-libinput xf86-video-fbdev xf86-video-vesa mesa-dri-gallium
	openbox tint2 xterm xsetroot pcmanfm font-dejavu font-noto adwaita-icon-theme
	firefox-esr netsurf
)

PKGS=("${PKGS_BASE[@]}")
[ "$PROFILE" = desktop ] && PKGS+=("${PKGS_DESKTOP[@]}")

rm -rf "$WORK"
mkdir -p "$ROOT/etc/apk" "$OUT/cache/apk"
cp -a /etc/apk/keys "$ROOT/etc/apk/"
cat > "$ROOT/etc/apk/repositories" <<EOF
$MIRROR/$ALPINE_BRANCH/main
$MIRROR/$ALPINE_BRANCH/community
EOF

log "installing ${#PKGS[@]} packages (profile=$PROFILE, alpine=$ALPINE_BRANCH)"
apk add --root "$ROOT" --initdb --arch x86_64 --no-progress \
	--repositories-file "$ROOT/etc/apk/repositories" \
	--cache-dir "$OUT/cache/apk" \
	"${PKGS[@]}" >"$WORK/apk.log" 2>&1 || { tail -40 "$WORK/apk.log"; exit 1; }
grep -iE 'error|warning' "$WORK/apk.log" | grep -v 'mkinitfs' | head -20 || true

log "installing FuhrerOS runtime and overlay"
cp -a /dist/. "$ROOT/"
cp -a /image/overlay/. "$ROOT/"
chmod +x "$ROOT/etc/init.d/fuhrerd" "$ROOT/etc/local.d/"*.start "$ROOT/usr/share/fuhrer/"*.sh "$ROOT/usr/bin/fuhrer-selftest"
chmod 0440 "$ROOT/etc/sudoers.d/wheel"

in_root() { chroot "$ROOT" /bin/sh -c "$*"; }

log "configuring services"
for s in devfs dmesg mdev hwdrivers; do in_root "rc-update add $s sysinit" >/dev/null; done
for s in modules sysctl hostname bootmisc syslog networking hwclock swap urandom localmount; do
	in_root "rc-update add $s boot" >/dev/null 2>&1 || true
done
for s in sshd chronyd fuhrerd local crond; do in_root "rc-update add $s default" >/dev/null; done
for s in mount-ro killprocs savecache; do in_root "rc-update add $s shutdown" >/dev/null; done
if [ "$PROFILE" = desktop ]; then
	# Xorg input needs udev; it replaces mdev for hotplug.
	in_root "rc-update del mdev sysinit; rc-update del hwdrivers sysinit" >/dev/null 2>&1 || true
	for s in udev udev-trigger udev-settle; do in_root "rc-update add $s sysinit" >/dev/null; done
	in_root "rc-update add udev-postmount default; rc-update add dbus default" >/dev/null
fi

log "accounts"
in_root "echo 'root:fuhrer' | chpasswd"
in_root "adduser -D -s /bin/bash -G users fuhrer && addgroup fuhrer wheel && echo 'fuhrer:fuhrer' | chpasswd"
for g in video input audio; do in_root "addgroup fuhrer $g" >/dev/null 2>&1 || true; done
in_root "sed -i 's#^root:x:0:0:root:/root:/bin/sh#root:x:0:0:root:/root:/bin/bash#' /etc/passwd"

# Host SSH key for test automation (generated once, kept in /out/ssh).
mkdir -p "$OUT/ssh"
[ -f "$OUT/ssh/id_ed25519" ] || ssh-keygen -q -t ed25519 -N '' -C fuhreros-dev -f "$OUT/ssh/id_ed25519"
install -d -m700 "$ROOT/root/.ssh" "$ROOT/home/fuhrer/.ssh"
install -m600 "$OUT/ssh/id_ed25519.pub" "$ROOT/root/.ssh/authorized_keys"
install -m600 "$OUT/ssh/id_ed25519.pub" "$ROOT/home/fuhrer/.ssh/authorized_keys"
in_root "chown -R fuhrer:users /home/fuhrer"
in_root "ssh-keygen -A" >/dev/null

if [ "$PROFILE" = desktop ]; then
	install -Dm644 /image/desktop/xinitrc "$ROOT/home/fuhrer/.xinitrc"
	install -Dm644 /image/desktop/autostart "$ROOT/home/fuhrer/.config/openbox/autostart"
	install -Dm644 /image/desktop/menu.xml "$ROOT/home/fuhrer/.config/openbox/menu.xml"
	install -Dm644 /image/desktop/bash_profile "$ROOT/home/fuhrer/.bash_profile"
	in_root "chown -R fuhrer:users /home/fuhrer"
	# tty1 logs the desktop user straight into X.
	sed -i 's#^tty1::.*#tty1::respawn:/sbin/agetty --autologin fuhrer --noclear 38400 tty1 linux#' "$ROOT/etc/inittab"
fi

KVER=$(ls "$ROOT/lib/modules" | head -1)
log "kernel $KVER: building initramfs"
cat > "$ROOT/etc/mkinitfs/mkinitfs.conf" <<'EOF'
features="ata base ext4 keymap kms mmc nvme scsi usb virtio"
EOF
mkinitfs -b "$ROOT" -c "$ROOT/etc/mkinitfs/mkinitfs.conf" -o "$WORK/initramfs-virt" "$KVER" >/dev/null
# Keep the initramfs inside the image too so it can boot from its own /boot.
cp "$WORK/initramfs-virt" "$ROOT/boot/initramfs-virt"

log "release metadata"
BUILD_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)
cat > "$ROOT/etc/fuhreros-release" <<EOF
FUHREROS_VERSION=0.4.0
FUHREROS_PROFILE=$PROFILE
FUHREROS_BUILD_DATE=$BUILD_DATE
FUHREROS_GIT_REV=$GIT_REV
FUHREROS_BASE=alpine-$(cat "$ROOT/etc/alpine-release")
FUHREROS_KERNEL=$KVER
EOF
in_root "apk info -v 2>/dev/null | sort" > "$OUT/packages-$PROFILE.txt"
rm -rf "$ROOT/var/cache/apk/"*

log "creating ${DISK_GB}G ext4 image"
IMG=$WORK/rootfs.img
truncate -s "${DISK_GB}G" "$IMG"
mke2fs -q -t ext4 -L fuhreros -E root_owner=0:0 -d "$ROOT" -F "$IMG"
e2fsck -fn "$IMG" >/dev/null

log "converting to qcow2"
NAME=fuhreros-$PROFILE
qemu-img convert -O qcow2 "$IMG" "$OUT/$NAME.qcow2.tmp"
mv "$OUT/$NAME.qcow2.tmp" "$OUT/$NAME.qcow2"
cp "$ROOT/boot/vmlinuz-virt" "$OUT/vmlinuz-$PROFILE"
cp "$WORK/initramfs-virt" "$OUT/initramfs-$PROFILE"

cat > "$OUT/build-info-$PROFILE.json" <<EOF
{
  "profile": "$PROFILE",
  "build_date": "$BUILD_DATE",
  "git_rev": "$GIT_REV",
  "alpine": "$(cat "$ROOT/etc/alpine-release")",
  "kernel": "$KVER",
  "disk_gb": $DISK_GB,
  "packages": $(wc -l < "$OUT/packages-$PROFILE.txt"),
  "rootfs_used_mb": $(du -sm "$ROOT" | cut -f1)
}
EOF
log "done: $OUT/$NAME.qcow2 (kernel $KVER)"
# Hand the artifacts back to the invoking host user.
[ -n "${HOST_UID:-}" ] && chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" "$OUT"
chmod 600 "$OUT/ssh/id_ed25519"
