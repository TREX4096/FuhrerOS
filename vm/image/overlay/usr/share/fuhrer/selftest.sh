#!/bin/sh
# FuhrerOS in-guest acceptance and compatibility checks (M0, M1, M14).
# Prints one "PASS|FAIL|SKIP <name>: detail" line per check and a summary.
# Usage: fuhrer-selftest [--offline]
OFFLINE=0
[ "$1" = "--offline" ] && OFFLINE=1
pass=0; fail=0; skip=0
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

check() { # name, command...
	name=$1; shift
	if out=$("$@" 2>&1); then
		pass=$((pass + 1)); echo "PASS $name: $(echo "$out" | tail -1 | cut -c1-90)"
	else
		fail=$((fail + 1)); echo "FAIL $name: $(echo "$out" | tail -3 | tr '\n' ' ' | cut -c1-200)"
	fi
}
skip() { skip=$((skip + 1)); echo "SKIP $1: $2"; }

echo "== FuhrerOS selftest ($(. /etc/fuhreros-release; echo "$FUHREROS_VERSION $FUHREROS_PROFILE"), kernel $(uname -r)) =="

# --- M0: VM acceptance ---
check kernel        sh -c 'uname -srm'
check userspace     sh -c 'rc-status -r'
check root-disk     sh -c 'findmnt -no SOURCE,FSTYPE / && test -w /'
check disk-write    sh -c "dd if=/dev/urandom of=$T/f bs=1M count=8 2>/dev/null && sync && sha256sum $T/f >$T/s && sha256sum -c $T/s"
check virtio-blk    sh -c 'ls /sys/block | grep -E "^vd[a-z]$"'
check net-iface     sh -c 'ip -o link show eth0 | cut -d" " -f2,9'
check net-ipv4      sh -c 'ip -4 -o addr show eth0 | awk "{print \$4}" | grep .'
check virtio-net    sh -c 'readlink /sys/class/net/eth0/device/driver | grep -o virtio_net'
check sshd          sh -c 'rc-service sshd status'

# --- M1/M2: FuhrerOS runtime ---
check fuhrerd       sh -c 'rc-service fuhrerd status'
check fuhrer-status sh -c 'fuhrer status | grep -E "^Policy engine"'
check fuhrer-profile sh -c 'fuhrer profile -n 1 -i 200 | tail -1'
check shm-state     sh -c 'test -s /run/fuhrer/state.shm && echo published'
check unit-adaptive /usr/libexec/fuhrer/test_adaptive
check unit-libfuhrer sh -c "cd $T && /usr/libexec/fuhrer/test_libfuhrer"
check io_uring      sh -c "fuhrer-bench storage --size 8M --time 1 --warmup 0 --backend batched --file $T/b.dat | grep -E 'batched/specialized=0/[1-9]' && echo io_uring-used"

# --- M14: application compatibility ---
check python        python3 -c 'import sys, json, sqlite3, ssl; print("python", sys.version.split()[0])'
check gcc           sh -c "printf '#include <stdio.h>\nint main(void){puts(\"hello from gcc\");return 0;}\n' > $T/h.c && gcc -O2 $T/h.c -o $T/h && $T/h"
check git           sh -c "cd $T && git init -q r && cd r && git -c user.email=t@t -c user.name=t commit -q --allow-empty -m x && git log --oneline | wc -l"
check editor        sh -c 'vim --version | head -1'
check text-browser  sh -c 'lynx -version | head -1'
if [ $OFFLINE = 0 ]; then
	check dns       sh -c 'nslookup example.com 2>/dev/null | grep -A1 "Name:" | tail -1 || getent hosts example.com'
	check curl      sh -c 'curl -fsS -o /dev/null -w "HTTP %{http_code} in %{time_total}s" https://example.com'
	check lynx-web  sh -c 'lynx -dump -nolist https://example.com | grep -m1 "Example Domain"'
else
	skip dns offline; skip curl offline; skip lynx-web offline
fi
if command -v firefox >/dev/null 2>&1 || command -v firefox-esr >/dev/null 2>&1; then
	check gui-browser sh -c 'firefox-esr --version 2>/dev/null || firefox --version'
else
	skip gui-browser "desktop profile not installed"
fi

echo "== summary: $pass passed, $fail failed, $skip skipped =="
[ $fail -eq 0 ]
