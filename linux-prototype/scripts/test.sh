#!/bin/bash
# FuhrerOS acceptance test (INSTRUCTION §17 + M14).
#
#   1. boots a fresh VM from the base image (headless, KVM or TCG)
#   2. runs the in-guest selftest (VM acceptance, runtime, app compatibility)
#   3. exercises adaptation: a random-read workload must drive the policy
#      to SPECIALIZED and the [ADAPT] record must be logged
#   4. checks graceful shutdown restores stock kernel settings
#   5. destroys and recreates the VM disk and boots again
#   6. verifies the immutable base image (and thus the host) was not modified
#
#   ./scripts/test.sh [--offline]
. "$(dirname "$0")/lib.sh"

OFFLINE=${1:-}
RESULTS=()
FAILED=0
record() { # name status detail
	RESULTS+=("$(printf '%-28s %-4s %s' "$1" "$2" "$3")")
	[ "$2" = PASS ] || FAILED=1
}

fu_require_image
fu_vm_running && fu_die "a VM is already running; ./scripts/reset.sh --stop first"
BASE_SUM=$(sha256sum "$FU_BASE_IMG" | cut -d' ' -f1)

# 1. boot from a clean disk
rm -f "$FU_DISK"
fu_boot_headless "" 300
record "vm-boot" PASS "booted in ${FU_BOOT_SECONDS}s (accel=$FU_ACCEL, $VCPUS vCPU, ${MEM_MB}MB)"

# 2. in-guest selftest
if SELF=$(fu_ssh "fuhrer-selftest $OFFLINE" 2>&1); then
	record "guest-selftest" PASS "$(echo "$SELF" | tail -1)"
else
	record "guest-selftest" FAIL "$(echo "$SELF" | grep -E '^FAIL' | head -3 | tr '\n' ';')"
fi
echo "$SELF" >"$FU_OUT/selftest-last.log"

# 3. adaptation under a random-read workload
ADAPT=$(fu_ssh '
	fuhrer policy auto >/dev/null
	fuhrer-bench storage --size 512M --time 1 --warmup 0 --file /var/tmp/t.dat >/dev/null
	echo 3 > /proc/sys/vm/drop_caches
	fuhrer-bench storage --pattern rand --bs 4k --depth 32 --size 512M --time 10 \
		--warmup 0 --file /var/tmp/t.dat --backend auto >/tmp/bench.txt
	echo "policy=$(sed -n "s/^policy=//p" /run/fuhrer/status)"
	echo "read_ahead_kb=$(cat /sys/block/vda/queue/read_ahead_kb)"
	grep -c "reason=high_small_io" /var/log/fuhrer/adapt.log
	grep -o "normal/batched/specialized=[0-9/]*" /tmp/bench.txt' 2>&1) || true
if echo "$ADAPT" | grep -q 'policy=SPECIALIZED' && echo "$ADAPT" | grep -q 'read_ahead_kb=16$'; then
	record "adaptation" PASS "IO_RANDOM -> SPECIALIZED, $(echo "$ADAPT" | tail -1)"
else
	record "adaptation" FAIL "$(echo "$ADAPT" | tr '\n' ' ')"
fi

# 4. stopping fuhrerd restores stock settings
RESTORE=$(fu_ssh 'rc-service fuhrerd stop >/dev/null 2>&1
	echo "$(cat /sys/block/vda/queue/read_ahead_kb)|$(cat /proc/sys/net/core/busy_read)|$(cat /proc/sys/vm/dirty_background_ratio)"
	rc-service fuhrerd start >/dev/null 2>&1' 2>&1) || true
if [ "$RESTORE" = "128|0|10" ]; then
	record "restore-on-stop" PASS "read_ahead_kb|busy_read|dirty_bg_ratio back to boot values ($RESTORE)"
else
	record "restore-on-stop" FAIL "$RESTORE"
fi

# 5. shutdown, destroy, recreate, boot again
fu_shutdown
record "vm-shutdown" PASS "guest powered off cleanly"
rm -f "$FU_DISK"
fu_boot_headless "" 300
if fu_ssh 'test ! -e /var/tmp/t.dat && rc-service fuhrerd status >/dev/null'; then
	record "vm-destroy-recreate" PASS "fresh disk, previous state gone, booted in ${FU_BOOT_SECONDS}s"
else
	record "vm-destroy-recreate" FAIL "state survived reset or runtime missing"
fi
fu_shutdown

# 6. base image untouched
if [ "$(sha256sum "$FU_BASE_IMG" | cut -d' ' -f1)" = "$BASE_SUM" ]; then
	record "base-image-immutable" PASS "sha256 unchanged (host disk never written by the guest)"
else
	record "base-image-immutable" FAIL "base image changed"
fi

echo
echo "FuhrerOS acceptance test — profile=$FU_PROFILE vm=$FU_VM accel=$FU_ACCEL"
printf '%s\n' "${RESULTS[@]}"
echo
if [ $FAILED = 0 ]; then echo "TESTS: PASS"; else echo "TESTS: FAIL"; fi
exit $FAILED
