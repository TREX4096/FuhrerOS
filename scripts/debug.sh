#!/bin/bash
# Boot with verbose kernel logging, a gdb stub on :1234 and QEMU guest-error
# tracing to $FU_OUT/qemu-debug.log.  --wait halts the CPU until gdb attaches:
#   gdb -ex 'target remote :1234'   (symbols: linux-virt-dev / vmlinux)
. "$(dirname "$0")/lib.sh"
WAIT=()
if [ "${1:-}" = --wait ]; then WAIT=(-S); shift; fi
[ "${1:-}" = -- ] && shift
fu_require_image
fu_detect_accel
fu_ensure_disk
fu_qemu_cmd debug "debug loglevel=7 ignore_loglevel $*"
fu_log "gdb stub on tcp::1234${WAIT:+ (CPU halted until gdb continues)}"
exec "${FU_QEMU[@]}" "${WAIT[@]}"
