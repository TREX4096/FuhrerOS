#!/bin/bash
# Shared helpers for FuhrerOS host scripts. Sourced, not executed.
#
# Environment knobs:
#   FUHRER_PROFILE  base | desktop          (default base)
#   FUHRER_VM       dev | bench | stress    (default dev; vm/configs/*.conf)
#   FUHRER_OUT      artifact directory      (default: see below)
#   FUHRER_ACCEL    kvm | tcg | whpx        (default: auto-detect)
#   FUHRER_WSL_DISTRO  WSL distribution used from Windows (default Ubuntu)

set -euo pipefail

# ---- Windows (Git Bash / MSYS) -> re-run inside WSL -------------------------
fu_reexec_in_wsl() {
	case "$(uname -s)" in
	MINGW* | MSYS* | CYGWIN*)
		local script win drive rest
		script=$(cd "$(dirname "$1")" && pwd -W)/$(basename "$1")
		drive=$(printf '%s' "${script:0:1}" | tr 'A-Z' 'a-z')
		rest=${script:2}
		shift
		export MSYS_NO_PATHCONV=1
		export WSLENV="FUHRER_PROFILE:FUHRER_VM:FUHRER_ACCEL:FUHRER_DISPLAY:FUHRER_REPS:FUHRER_TIME:${WSLENV:-}"
		exec wsl.exe -d "${FUHRER_WSL_DISTRO:-Ubuntu}" -- bash "/mnt/$drive$rest" "$@"
		;;
	esac
}
# Sourced without arguments, "$@" is still the calling script's arguments.
fu_reexec_in_wsl "$0" "$@"

FU_REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
FU_PROFILE=${FUHRER_PROFILE:-base}
FU_VM=${FUHRER_VM:-dev}

# Keep VM disks on a native Linux filesystem: disks on /mnt/<drive> (9p/drvfs)
# are slow and add host-side noise to every storage measurement (D-003).
if [ -n "${FUHRER_OUT:-}" ]; then
	FU_OUT=$FUHRER_OUT
elif [[ $FU_REPO == /mnt/* ]]; then
	FU_OUT=$HOME/.cache/fuhreros/out
else
	FU_OUT=$FU_REPO/out
fi
mkdir -p "$FU_OUT"

FU_CONF=$FU_REPO/vm/configs/$FU_VM.conf
[ -f "$FU_CONF" ] || { echo "unknown VM config '$FU_VM' ($FU_CONF)" >&2; exit 2; }
# shellcheck disable=SC1090
. "$FU_CONF"

FU_BASE_IMG=$FU_OUT/fuhreros-$FU_PROFILE.qcow2
FU_KERNEL=$FU_OUT/vmlinuz-$FU_PROFILE
FU_INITRD=$FU_OUT/initramfs-$FU_PROFILE
FU_DISK=$FU_OUT/vm-$FU_VM-$FU_PROFILE.qcow2
FU_PIDFILE=$FU_OUT/vm-$FU_VM-$FU_PROFILE.pid
FU_SERIAL_LOG=$FU_OUT/vm-$FU_VM-$FU_PROFILE.serial.log
FU_MONITOR=$FU_OUT/vm-$FU_VM-$FU_PROFILE.monitor
FU_SSH_KEY=$FU_OUT/ssh/id_ed25519

fu_log() { printf '\033[1;34m[fuhreros]\033[0m %s\n' "$*" >&2; }
fu_die() { printf '\033[1;31m[fuhreros] error:\033[0m %s\n' "$*" >&2; exit 1; }

fu_require_image() {
	for f in "$FU_BASE_IMG" "$FU_KERNEL" "$FU_INITRD"; do
		[ -f "$f" ] || fu_die "missing $f — run ./scripts/build.sh first (FUHRER_PROFILE=$FU_PROFILE)"
	done
	command -v qemu-system-x86_64 >/dev/null || fu_die "qemu-system-x86_64 not found"
}

# Never assume KVM exists (INSTRUCTION M-§7).
fu_detect_accel() {
	if [ -n "${FUHRER_ACCEL:-}" ]; then
		FU_ACCEL=$FUHRER_ACCEL
	elif [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
		FU_ACCEL=kvm
	else
		FU_ACCEL=tcg
	fi
	case $FU_ACCEL in
	kvm) FU_CPU=host ;;
	*) FU_CPU=max ;;
	esac
	echo "Acceleration: $(printf '%s' "$FU_ACCEL" | tr 'a-z' 'A-Z')" >&2
	if [ "$FU_ACCEL" = tcg ] && [ -e /dev/kvm ]; then
		fu_log "hint: /dev/kvm exists but is not accessible; 'sudo usermod -aG kvm $USER' enables KVM"
	fi
}

# Copy-on-write disk on top of the immutable base image: resetting the VM
# is just deleting this file, and the base (and the host) are never modified.
fu_ensure_disk() {
	if [ ! -f "$FU_DISK" ]; then
		fu_log "creating VM disk $FU_DISK (backing: $(basename "$FU_BASE_IMG"), ${DISK_GB}G)"
		qemu-img create -q -f qcow2 -F qcow2 -b "$FU_BASE_IMG" "$FU_DISK" "${DISK_GB}G"
	fi
}

fu_vm_running() {
	[ -f "$FU_PIDFILE" ] && kill -0 "$(cat "$FU_PIDFILE")" 2>/dev/null
}

# Build the QEMU command line into the FU_QEMU array.
#   $1 = console mode: interactive | headless | debug
#   $2 = extra kernel parameters
fu_qemu_cmd() {
	local mode=$1 extra=${2:-} cache=none
	# O_DIRECT is unavailable on drvfs; fall back to host page cache there.
	[[ $FU_OUT == /mnt/* ]] && cache=writeback
	local append="root=/dev/vda rootfstype=ext4 modules=ext4,virtio_blk,virtio_pci rw"
	append+=" console=tty0 console=ttyS0,115200 $extra"
	FU_QEMU=(
		qemu-system-x86_64
		-name "fuhreros-$FU_VM"
		-machine "q35,accel=$FU_ACCEL"
		-cpu "$FU_CPU"
		-smp "$VCPUS"
		-m "$MEM_MB"
		-kernel "$FU_KERNEL"
		-initrd "$FU_INITRD"
		-append "$append"
		-drive "file=$FU_DISK,if=virtio,format=qcow2,cache=$cache,discard=unmap"
		-netdev "user,id=net0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22"
		-device virtio-net-pci,netdev=net0
		-device virtio-rng-pci
		-no-reboot
	)
	if [ "$FU_PROFILE" = desktop ]; then
		local disp=${FUHRER_DISPLAY:-gtk}
		[ "$mode" = headless ] && disp=none
		FU_QEMU+=(-vga virtio -display "$disp" -device qemu-xhci -device usb-tablet -device usb-kbd)
	else
		FU_QEMU+=(-display none -vga none)
	fi
	case $mode in
	interactive) FU_QEMU+=(-serial mon:stdio) ;;
	headless) FU_QEMU+=(-serial "file:$FU_SERIAL_LOG" -monitor "unix:$FU_MONITOR,server,nowait"
		-daemonize -pidfile "$FU_PIDFILE") ;;
	debug) FU_QEMU+=(-serial mon:stdio -s -d guest_errors,unimp -D "$FU_OUT/qemu-debug.log") ;;
	esac
}

# Send one command to the QEMU human monitor of a headless VM.
fu_monitor() {
	printf '%s\n' "$*" | nc -q1 -U "$FU_MONITOR" >/dev/null 2>&1 ||
		printf '%s\n' "$*" | nc -N -U "$FU_MONITOR" >/dev/null 2>&1
}

fu_ssh() {
	ssh -q -i "$FU_SSH_KEY" -p "$SSH_PORT" \
		-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		-o ConnectTimeout=5 -o LogLevel=ERROR root@127.0.0.1 "$@"
}

fu_scp_from() { # remote local
	scp -q -i "$FU_SSH_KEY" -P "$SSH_PORT" \
		-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
		"root@127.0.0.1:$1" "$2"
}

fu_scp_to() { # local remote
	scp -q -i "$FU_SSH_KEY" -P "$SSH_PORT" \
		-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
		"$1" "root@127.0.0.1:$2"
}

# Start a headless VM and wait until the boot marker and SSH are up.
fu_boot_headless() { # extra-kernel-args timeout_s
	local extra=${1:-} timeout=${2:-300}
	fu_require_image
	fu_vm_running && fu_die "VM already running (pid $(cat "$FU_PIDFILE")); ./scripts/reset.sh --stop"
	fu_detect_accel
	fu_ensure_disk
	: >"$FU_SERIAL_LOG"
	fu_qemu_cmd headless "$extra"
	"${FU_QEMU[@]}"
	local t0=$SECONDS
	fu_log "booting (pid $(cat "$FU_PIDFILE")), serial log: $FU_SERIAL_LOG"
	until grep -q FUHREROS-BOOT-COMPLETE "$FU_SERIAL_LOG" 2>/dev/null; do
		fu_vm_running || { tail -30 "$FU_SERIAL_LOG" >&2; fu_die "VM exited during boot"; }
		((SECONDS - t0 < timeout)) || { tail -30 "$FU_SERIAL_LOG" >&2; fu_die "boot timeout (${timeout}s)"; }
		sleep 1
	done
	FU_BOOT_SECONDS=$((SECONDS - t0))
	until fu_ssh true 2>/dev/null; do
		((SECONDS - t0 < timeout)) || fu_die "ssh timeout"
		sleep 1
	done
	fu_log "guest up in ${FU_BOOT_SECONDS}s (ssh after $((SECONDS - t0))s)"
}

fu_shutdown() {
	fu_vm_running || return 0
	fu_ssh poweroff >/dev/null 2>&1 || true
	local i
	for i in $(seq 1 60); do
		fu_vm_running || { rm -f "$FU_PIDFILE"; return 0; }
		sleep 1
	done
	fu_log "guest did not power off; killing QEMU"
	kill "$(cat "$FU_PIDFILE")" 2>/dev/null || true
	rm -f "$FU_PIDFILE"
}
