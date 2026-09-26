#!/bin/bash
# QEMU halted at reset with a GDB stub, plus a GDB session with kernel symbols.
#   ./scripts/run-debug.sh            (QEMU in background, GDB in foreground)
#   ./scripts/run-debug.sh --no-gdb   (just wait for a debugger on :1234)
. "$(dirname "$0")/common.sh"
ISO=${FUHRER_ISO:-$B/fuhreros.iso}
[ -f "$ISO" ] || die "missing $ISO — run ./scripts/build.sh"
FUHRER_ACCEL=${FUHRER_ACCEL:-tcg} detect_accel   # TCG: breakpoints are reliable
qemu_base "$ISO" "$B/disk.img"
QEMU+=(-serial "file:$B/debug-serial.log" -display none -s -S -d int,guest_errors -D "$B/qemu-debug.log")
if [ "${1:-}" = --no-gdb ]; then
	log "GDB stub on :1234; serial -> build/debug-serial.log"
	exec "${QEMU[@]}"
fi
"${QEMU[@]}" &
QPID=$!
trap 'kill $QPID 2>/dev/null' EXIT
gdb -q "$B/fuhreros.elf" -ex 'set disassembly-flavor intel' -ex 'target remote :1234' \
	-ex 'hbreak kmain' -ex 'continue'
