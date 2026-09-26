#!/bin/bash
# Destroy the VM's writable disk and recreate it from the immutable base image.
#   ./scripts/reset.sh          stop VM (if running) + recreate disk
#   ./scripts/reset.sh --stop   only stop a running headless VM
. "$(dirname "$0")/lib.sh"
if fu_vm_running; then
	fu_log "stopping VM (pid $(cat "$FU_PIDFILE"))"
	fu_shutdown
fi
[ "${1:-}" = --stop ] && exit 0
rm -f "$FU_DISK" "$FU_SERIAL_LOG"
fu_require_image
fu_ensure_disk
echo "VM disk reset: $FU_DISK"
