#!/bin/bash
# Power-off / reboot check without QEMU's isa-debug-exit device, so only the
# kernel's ACPI S5 path (poweroff) or its reset path (reboot) can end QEMU
# (QEMU runs with -no-reboot, so a reset also exits).
#   ./scripts/test-power.sh poweroff|reboot      -> POWER: PASS / FAIL
. "$(dirname "$0")/common.sh"
WHAT=${1:-poweroff}
[ -f "$B/fuhreros.iso" ] || die "build first (./scripts/build.sh)"
cp --sparse=always "$B/disk.img" "$B/disk-power.img"
detect_accel
qemu_base "$B/fuhreros.iso" "$B/disk-power.img"
Q=()
for ((i = 0; i < ${#QEMU[@]}; i++)); do
	if [ "${QEMU[i]}" = "-device" ] && [[ ${QEMU[i + 1]} == isa-debug-exit* ]]; then
		i=$((i + 1))
		continue
	fi
	Q+=("${QEMU[i]}")
done
MON=$B/power.monitor
rm -f "$MON"
"${Q[@]}" -serial "file:$B/power-serial.log" -display none -vga std -monitor "unix:$MON,server,nowait" &
P=$!
key() { printf 'sendkey %s\n' "$1" | nc -q1 -U "$MON" >/dev/null 2>&1; sleep 0.3; }
sleep 18
key ctrl-alt-t
sleep 2
for k in $(echo "$WHAT" | sed 's/./& /g') ret; do key "$k"; done
for _ in $(seq 1 30); do kill -0 $P 2>/dev/null || break; sleep 0.5; done
if kill -0 $P 2>/dev/null; then
	kill $P
	echo "POWER: FAIL ($WHAT did not end the VM)"
	exit 1
fi
wait $P
rc=$?
grep -a -E "acpi.*S5|POWEROFF|REBOOT" "$B/power-serial.log" | tail -2
echo "POWER: PASS ($WHAT ended the VM, QEMU exit code $rc, no isa-debug-exit device)"
