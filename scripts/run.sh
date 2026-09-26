#!/bin/bash
# Boot FuhrerOS in QEMU with the serial console on this terminal.
#   ./scripts/run.sh [--headless] [-- extra kernel parameters]
# Examples:
#   ./scripts/run.sh                               # dev VM, interactive console
#   FUHRER_VM=bench ./scripts/run.sh               # 4 vCPU / 4 GB benchmark VM
#   ./scripts/run.sh -- fuhrer.mode=observe        # A2 ablation at boot
#   ./scripts/run.sh -- fuhrer.disable=1           # B0: plain Linux, no FuhrerOS layer
# Ctrl-A X quits QEMU. SSH: ./scripts/ssh.sh
. "$(dirname "$0")/lib.sh"

HEADLESS=0
if [ "${1:-}" = --headless ]; then HEADLESS=1; shift; fi
[ "${1:-}" = -- ] && shift
EXTRA="$*"

if [ $HEADLESS = 1 ]; then
	fu_boot_headless "$EXTRA"
	echo "VM running headless. ./scripts/ssh.sh to log in, ./scripts/reset.sh --stop to stop."
	exit 0
fi

fu_require_image
fu_vm_running && fu_die "VM already running (pid $(cat "$FU_PIDFILE"))"
fu_detect_accel
fu_ensure_disk
fu_qemu_cmd interactive "$EXTRA"
fu_log "VM: $FU_VM ($VCPUS vCPU, $MEM_MB MB, ${DISK_GB}G disk, virtio-net, ssh -> 127.0.0.1:$SSH_PORT)"
fu_log "Ctrl-A X quits; Ctrl-A C toggles the QEMU monitor"
exec "${FU_QEMU[@]}"
