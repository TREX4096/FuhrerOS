#!/bin/sh
# Guest side of scripts/benchmark.sh. One invocation = one measured run.
#
#   bench-guest.sh prepare <size_mb>
#   bench-guest.sh run <config> <workload> <outfile.json> <time_s> <warmup_s>
#   bench-guest.sh sysinfo
#   bench-guest.sh daemon-config <file>     (restart fuhrerd with a config)
#
# config:   B0 (no FuhrerOS daemon) | normal | batched | specialized | adaptive
#           | off | observe | keep | knobs-<p> | lib-<p>   (ablations)
# workload: randread | seqread | randwrite | seqwrite | cpu | memory
#           | netlat | netbw | transition
set -eu
DATA=/var/tmp/fuhrer-bench.dat

set_mode() {
	case $1 in
	B0)
		if [ -e /run/fuhrer/fuhrerd.pid ] && kill -0 "$(cat /run/fuhrer/fuhrerd.pid)" 2>/dev/null; then
			echo "B0 requested but fuhrerd is running" >&2; exit 3
		fi
		BACKEND=normal ;;
	normal | batched | specialized)
		fuhrer policy "$1" >/dev/null; BACKEND=$1 ;;
	adaptive)
		fuhrer policy auto >/dev/null; BACKEND=auto ;;
	off | observe)
		fuhrer policy "$1" >/dev/null; BACKEND=normal ;;
	knobs-batched | knobs-specialized)	# A6: kernel tunables only
		fuhrer policy "${1#knobs-}" >/dev/null; BACKEND=normal ;;
	lib-batched | lib-specialized)		# A6: libfuhrer backend only
		fuhrer policy normal >/dev/null; BACKEND=${1#lib-} ;;
	keep)	# leave the daemon exactly as configured (ablations)
		BACKEND=auto ;;
	*) echo "unknown config $1" >&2; exit 2 ;;
	esac
}

settle() {
	sync
	echo 3 >/proc/sys/vm/drop_caches
	sleep 1
}

case $1 in
prepare)
	size=$2
	have=$(stat -c %s "$DATA" 2>/dev/null || echo 0)
	if [ "$have" -lt $((size * 1024 * 1024)) ]; then
		echo "preparing ${size} MiB data file"
		dd if=/dev/urandom of="$DATA" bs=1M count="$size" conv=fsync status=none
	fi
	;;
run)
	config=$2 workload=$3 out=$4 t=$5 w=$6
	set_mode "$config"
	sz=$(($(stat -c %s "$DATA") / 1024 / 1024))M
	common="--time $t --warmup $w --json $out"
	case $workload in
	randread)  settle; fuhrer-bench storage --pattern rand --op read  --bs 4k --depth 32 --size "$sz" --file "$DATA" --backend $BACKEND $common ;;
	seqread)   settle; fuhrer-bench storage --pattern seq  --op read  --bs 1M --depth 4  --size "$sz" --file "$DATA" --backend $BACKEND $common ;;
	randwrite) settle; fuhrer-bench storage --pattern rand --op write --bs 4k --depth 32 --size "$sz" --file "$DATA" --backend $BACKEND $common ;;
	seqwrite)  settle; fuhrer-bench storage --pattern seq  --op write --bs 1M --depth 4  --size "$sz" --file "$DATA" --backend $BACKEND $common ;;
	cpu)       fuhrer-bench cpu $common ;;
	memory)    fuhrer-bench memory --size 256M $common ;;
	netlat)    fuhrer-bench net --mode latency --msg 64 $common ;;
	netbw)     fuhrer-bench net --mode throughput $common ;;
	transition)
		settle
		fuhrer-bench transition --phase-time "$t" --depth 32 --size "$sz" --file "$DATA" \
			--timeline "${out%.json}.timeline.csv"
		;;
	*) echo "unknown workload $workload" >&2; exit 2 ;;
	esac
	# Record what the daemon believed at the end of the run.
	[ -r /run/fuhrer/status ] && cp /run/fuhrer/status "${out%.json}.status" || true
	;;
sysinfo)
	echo "== fuhreros-release"; cat /etc/fuhreros-release
	echo "== uname"; uname -a
	echo "== cmdline"; cat /proc/cmdline
	echo "== cpu"; grep -m1 'model name' /proc/cpuinfo; nproc
	echo "== memory"; grep -E 'MemTotal|MemAvailable' /proc/meminfo
	echo "== block"; lsblk -o NAME,SIZE,ROTA,SCHED,MODEL 2>/dev/null || cat /proc/partitions
	for f in scheduler read_ahead_kb nr_requests nomerges rq_affinity; do
		echo "vda/$f: $(cat /sys/block/vda/queue/$f)"
	done
	echo "== network"; ip -o addr show eth0; ethtool -i eth0 2>/dev/null | head -2 || true
	echo "== fuhrerd"; cat /run/fuhrer/status 2>/dev/null | head -8 || echo "not running"
	echo "== config"; cat /etc/fuhrer/fuhrer.conf | grep -vE '^\s*(#|$)'
	;;
daemon-config)
	cp "$2" /etc/fuhrer/fuhrer.conf.active
	echo "FUHRERD_CONFIG=/etc/fuhrer/fuhrer.conf.active" >/etc/conf.d/fuhrerd
	rc-service fuhrerd restart >/dev/null 2>&1
	sleep 2
	;;
*)
	sed -n '2,15p' "$0"; exit 2 ;;
esac
