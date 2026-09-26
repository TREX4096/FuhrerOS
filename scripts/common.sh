#!/bin/bash
# Shared helpers for FuhrerOS scripts (sourced).
set -euo pipefail

# From Git Bash on Windows, re-run inside WSL (the development environment).
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*)
	script=$(cd "$(dirname "$0")" && pwd -W)/$(basename "$0")
	drive=$(printf '%s' "${script:0:1}" | tr 'A-Z' 'a-z')
	export MSYS_NO_PATHCONV=1
	exec wsl.exe -d "${FUHRER_WSL_DISTRO:-Ubuntu}" -- bash "/mnt/$drive${script:2}" "$@"
	;;
esac

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
if [[ $REPO == /mnt/* ]]; then B=$HOME/.cache/fuhreros/build; else B=$REPO/build; fi
export B
export PATH="$HOME/opt/cross/bin:$PATH"
VCPUS=${FUHRER_VCPUS:-2}
MEM=${FUHRER_MEM:-2048}
DISK_GB=${FUHRER_DISK_GB:-20}

log() { printf '\033[1;34m[fuhreros]\033[0m %s\n' "$*" >&2; }
die() { printf '\033[1;31m[fuhreros] error:\033[0m %s\n' "$*" >&2; exit 1; }

# Never silently fall back (NEW_EXPLANATION §5): always print the choice.
detect_accel() {
	if [ -n "${FUHRER_ACCEL:-}" ]; then
		ACCEL=$FUHRER_ACCEL
	elif [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
		ACCEL=kvm
	else
		ACCEL=tcg
	fi
	case $ACCEL in
	kvm) CPU=host ;;
	whpx) CPU=max ;;
	*) CPU=max ;;
	esac
	echo "Acceleration: $(echo "$ACCEL" | tr a-z A-Z)" >&2
	[ "$ACCEL" = tcg ] && [ -e /dev/kvm ] && log "note: /dev/kvm exists but is not usable by $USER; using TCG"
	return 0
}

# Common QEMU machine: q35, virtio disk + net, serial, isa-debug-exit for tests.
qemu_base() { # iso disk
	QEMU=(qemu-system-x86_64 -machine "q35,accel=$ACCEL" -cpu "$CPU" -smp "$VCPUS" -m "$MEM"
		-cdrom "$1" -boot d
		-device isa-debug-exit,iobase=0xf4,iosize=0x04
		-netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${FUHRER_HTTP_PORT:-8080}-:80,hostfwd=tcp:127.0.0.1:${FUHRER_ECHO_PORT:-7777}-:7"
		-device virtio-net-pci,netdev=net0,disable-legacy=on
		-no-reboot)
	if [ -n "${2:-}" ] && [ -f "$2" ]; then
		QEMU+=(-drive "file=$2,if=none,id=disk0,format=raw,cache=${FUHRER_DISK_CACHE:-writeback}"
			-device virtio-blk-pci,drive=disk0,disable-legacy=on)
	fi
}
