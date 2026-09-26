#!/bin/bash
# Boot the self-test ISO headless and check every stage's TEST line.
# Exit status of QEMU: 3 = all passed (qemu_exit(1)), 5 = failures, else crash/timeout.
. "$(dirname "$0")/common.sh"
ISO=$B/fuhreros-test.iso
[ -f "$ISO" ] || die "missing $ISO — run ./scripts/build.sh"
LOG=$B/test-serial.log
detect_accel
rm -f "$B/disk-test.img"
[ -f "$B/disk.img" ] && cp --sparse=always "$B/disk.img" "$B/disk-test.img"
qemu_base "$ISO" "$B/disk-test.img"
QEMU+=(-serial "file:$LOG" -display none -vga std)
set +e
timeout "${FUHRER_TEST_TIMEOUT:-300}" "${QEMU[@]}"
rc=$?
set -e
grep -E '^TEST ' "$LOG" | sed 's/\r$//' || true
# Evidence that the running kernel is ours and Linux never ran.
if grep -q "Linux version" "$LOG"; then echo "VERIFY linux-absent FAIL"; rc=99; else echo "VERIFY linux-absent PASS (no Linux banner on the console)"; fi
grep -q "FUHREROS KERNEL" "$LOG" && echo "VERIFY fuhreros-banner PASS" || { echo "VERIFY fuhreros-banner FAIL"; rc=99; }
case $rc in
3) echo "TESTS: PASS" ; exit 0 ;;
5) echo "TESTS: FAIL (some self-tests failed)"; exit 1 ;;
124) echo "TESTS: FAIL (timeout)"; tail -30 "$LOG"; exit 1 ;;
*) echo "TESTS: FAIL (qemu exit $rc)"; tail -40 "$LOG"; exit 1 ;;
esac
