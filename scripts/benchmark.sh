#!/bin/bash
# FuhrerOS experiment runner (INSTRUCTION §19–§23, M5/M15).
#
#   ./scripts/benchmark.sh [--suite main|transition|ablation|all] [--reps N]
#                          [--time S] [--warmup S] [--workloads a,b,c] [--quick]
#
# Suites
#   main        B0 native (fuhrer.disable=1) vs static normal (B1) / batched /
#               specialized vs adaptive (B2); B3 = best static per workload;
#               A6 decomposition: knobs-<p> (kernel tunables only) and
#               lib-<p> (libfuhrer backend only)
#   transition  phase-changing workload under adaptive vs static configs (Graph 3)
#   ablation    A1 off, A2 observe, A4 adaptive, A5 intervals, A7 hysteresis
#               on the transition workload + monitoring overhead on cpu (Graph 4)
#
# Output: experiments/E-NNN-<suite>-<date>/ with config.json, workload.json,
# system-info.txt, metrics.csv, stdout.log, stderr.log, raw/, result.json,
# summary.md and SVG graphs. Nothing is reported that was not measured.
export FUHRER_VM=${FUHRER_VM:-bench}
. "$(dirname "$0")/lib.sh"

SUITE=main REPS=${FUHRER_REPS:-3} TIME=${FUHRER_TIME:-10} WARMUP=5
WORKLOADS=randread,seqread,randwrite,seqwrite,cpu,memory,netlat,netbw
STATIC_CONFIGS=(normal batched specialized adaptive knobs-batched knobs-specialized lib-batched lib-specialized)
while [ $# -gt 0 ]; do
	case $1 in
	--suite) SUITE=$2; shift ;;
	--reps) REPS=$2; shift ;;
	--time) TIME=$2; shift ;;
	--warmup) WARMUP=$2; shift ;;
	--workloads) WORKLOADS=$2; shift ;;
	--quick) REPS=1 TIME=5 WARMUP=4 WORKLOADS=randread,seqread,cpu,netlat ;;
	--configs) IFS=, read -r -a STATIC_CONFIGS <<<"$2"; shift ;;
	*) fu_die "unknown option $1" ;;
	esac
	shift
done
IFS=, read -r -a WLS <<<"$WORKLOADS"

command -v python3 >/dev/null || fu_die "python3 needed on the host for analysis"
fu_vm_running && fu_die "a VM is already running; ./scripts/reset.sh --stop first"

# ---- experiment directory -------------------------------------------------
mkdir -p "$FU_REPO/experiments"
N=$(find "$FU_REPO/experiments" -maxdepth 1 -name 'E-[0-9]*' | sed -E 's#.*/E-0*([0-9]+)-.*#\1#' | sort -n | tail -1)
N=$(printf '%03d' $((10#${N:-0} + 1)))
EXP=$FU_REPO/experiments/E-$N-$SUITE-$(date +%Y%m%d-%H%M)
mkdir -p "$EXP/raw"
exec > >(tee -a "$EXP/stdout.log") 2> >(tee -a "$EXP/stderr.log" >&2)
fu_log "experiment $EXP"

REV=$(git -C "$FU_REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)
[ -n "$(git -C "$FU_REPO" status --porcelain 2>/dev/null)" ] && REV="$REV-dirty"
fu_detect_accel 2>/dev/null
cat >"$EXP/config.json" <<EOF
{
  "experiment": "E-$N",
  "suite": "$SUITE",
  "date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "commit": "$REV",
  "host": "$(uname -srm)",
  "host_cpu": "$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs)",
  "host_mem_mb": $(awk '/MemTotal/{print int($2/1024)}' /proc/meminfo),
  "host_note": "WSL2 (Hyper-V) host, KVM nested inside WSL2 when accel=kvm",
  "qemu": "$(qemu-system-x86_64 --version | head -1)",
  "accel": "$FU_ACCEL",
  "vm_config": "$FU_VM",
  "vcpus": $VCPUS,
  "memory_mb": $MEM_MB,
  "disk_gb": $DISK_GB,
  "disk": "virtio-blk, qcow2 overlay on ext4 (WSL2 vhdx), cache=none",
  "network": "virtio-net, QEMU user-mode (slirp)",
  "profile": "$FU_PROFILE",
  "image": $(cat "$FU_OUT/build-info-$FU_PROFILE.json"),
  "reps": $REPS,
  "time_s": $TIME,
  "warmup_s": $WARMUP,
  "workloads": "$(IFS=,; echo "${WLS[*]}")",
  "sampling_interval_ms": 1000,
  "hysteresis": 3,
  "min_dwell_ms": 3000,
  "policy_map": "v2"
}
EOF

workload_json() {
	cat >"$EXP/workload.json" <<'EOF'
{
  "randread":  "fuhrer-bench storage rand read 4KiB qd32, file 1.5x guest RAM, caches dropped",
  "seqread":   "fuhrer-bench storage seq read 1MiB qd4, caches dropped",
  "randwrite": "fuhrer-bench storage rand write 4KiB qd32 (+fdatasync at end)",
  "seqwrite":  "fuhrer-bench storage seq write 1MiB qd4 (+fdatasync at end)",
  "cpu":       "fuhrer-bench cpu, 1 thread per vCPU, xorshift/multiply loop, 1s slices",
  "memory":    "fuhrer-bench memory 256MiB STREAM triad + pointer chase",
  "netlat":    "fuhrer-bench net loopback TCP 64B ping-pong, TCP_NODELAY",
  "netbw":     "fuhrer-bench net loopback TCP 128KiB writes",
  "transition":"fuhrer-bench transition idle->cpu->randread->seqread->idle, phase=time_s"
}
EOF
}
workload_json

G=/usr/share/fuhrer/bench-guest.sh
run_one() { # config workload rep
	local out=/var/tmp/fuhrer-exp/$1/$2/rep$3.json
	fu_ssh "mkdir -p $(dirname "$out") && $G run $1 $2 $out $TIME $WARMUP" 2>&1 |
		grep -E '^(storage|cpu|memory|net|transition|done)' | sed "s/^/  [$1 rep$3] /" || true
}

boot() { # extra-cmdline tag
	fu_boot_headless "$1" 300
	fu_ssh "$G sysinfo" >>"$EXP/system-info.txt" 2>&1 || true
	echo "boot[$2]: ${FU_BOOT_SECONDS}s" >>"$EXP/system-info.txt"
	local ram_mb
	ram_mb=$(fu_ssh "awk '/MemTotal/{print int(\$2/1024)}' /proc/meminfo")
	fu_ssh "$G prepare $((ram_mb * 3 / 2))"
}

collect() { # tag
	fu_ssh 'tar -C /var/tmp -cf - fuhrer-exp 2>/dev/null' | tar -C "$EXP/raw" -xf - || true
	fu_ssh 'cat /var/log/fuhrer/metrics.csv 2>/dev/null' >"$EXP/metrics-$1.csv" || true
	fu_ssh 'cat /var/log/fuhrer/adapt.log 2>/dev/null' >"$EXP/adapt-$1.log" || true
	fu_ssh 'rm -rf /var/tmp/fuhrer-exp' || true
}

shuffle() { printf '%s\n' "$@" | shuf; }

suite_main() {
	rm -f "$FU_DISK"
	fu_log "B0: native Linux (fuhrer.disable=1)"
	boot "fuhrer.disable=1" B0
	for rep in $(seq 1 "$REPS"); do
		for wl in "${WLS[@]}"; do run_one B0 "$wl" "$rep"; done
	done
	collect B0
	fu_shutdown

	fu_log "B1/B2/B3: FuhrerOS static and adaptive"
	boot "" fuhrer
	for rep in $(seq 1 "$REPS"); do
		# Interleave configurations so slow host drift hits all of them alike.
		for cfg in $(shuffle "${STATIC_CONFIGS[@]}"); do
			for wl in "${WLS[@]}"; do run_one "$cfg" "$wl" "$rep"; done
		done
	done
	collect fuhrer
	fu_shutdown
}

suite_transition() {
	[ -f "$FU_DISK" ] || true
	boot "" transition
	for rep in $(seq 1 "$REPS"); do
		for cfg in $(shuffle normal batched specialized adaptive); do
			fu_log "transition: $cfg rep$rep"
			run_one "$cfg" transition "$rep"
		done
	done
	collect transition
	fu_shutdown
}

abl_conf() { # name key=value...
	local f=$EXP/raw/ablation-$1.conf
	mkdir -p "$EXP/raw"
	grep -vE '^\s*(#|$)' "$FU_REPO/vm/image/overlay/etc/fuhrer/fuhrer.conf" >"$f"
	shift
	local kv
	for kv in "$@"; do
		sed -i "/^${kv%%=*}\s*=/d" "$f"
		echo "${kv%%=*} = ${kv#*=}" >>"$f"
	done
	fu_scp_to "$f" /tmp/abl.conf
	fu_ssh "$G daemon-config /tmp/abl.conf"
}

suite_ablation() {
	boot "" ablation
	# Graph 4 / A1-A2: monitoring overhead on a CPU-bound workload.
	for rep in $(seq 1 "$REPS"); do
		for v in off observe-1000 observe-100 observe-10; do
			case $v in
			off) abl_conf off mode=off ;;
			observe-*) abl_conf "$v" mode=observe "interval_ms=${v#observe-}" ;;
			esac
			fu_ssh "mkdir -p /var/tmp/fuhrer-exp/overhead-$v/cpu && $G run keep cpu /var/tmp/fuhrer-exp/overhead-$v/cpu/rep$rep.json $TIME $WARMUP" 2>&1 |
				grep -E '^cpu' | sed "s/^/  [$v rep$rep] /" || true
			fu_ssh "cp /run/fuhrer/status /var/tmp/fuhrer-exp/overhead-$v/cpu/rep$rep.daemon-status 2>/dev/null" || true
		done
	done
	# A4/A5/A7 on the transition workload.
	for rep in $(seq 1 "$REPS"); do
		for v in adaptive-default interval-100 interval-5000 hyst-1 hyst-5; do
			case $v in
			adaptive-default) abl_conf "$v" mode=adaptive ;;
			interval-*) abl_conf "$v" mode=adaptive "interval_ms=${v#interval-}" ;;
			hyst-*) abl_conf "$v" mode=adaptive "hysteresis=${v#hyst-}" ;;
			esac
			fu_log "ablation $v rep$rep"
			fu_ssh "mkdir -p /var/tmp/fuhrer-exp/abl-$v/transition && $G run keep transition /var/tmp/fuhrer-exp/abl-$v/transition/rep$rep.json $TIME $WARMUP" >/dev/null 2>&1 || true
			fu_ssh "cp /var/log/fuhrer/adapt.log /var/tmp/fuhrer-exp/abl-$v/transition/rep$rep.adapt.log" || true
		done
	done
	fu_ssh "rm -f /etc/conf.d/fuhrerd /etc/fuhrer/fuhrer.conf.active; rc-service fuhrerd restart >/dev/null 2>&1" || true
	collect ablation
	fu_shutdown
}

case $SUITE in
main) suite_main ;;
transition) suite_transition ;;
ablation) suite_ablation ;;
all) suite_main; suite_transition; suite_ablation ;;
*) fu_die "unknown suite $SUITE" ;;
esac

# metrics.csv = fuhrerd samples from every boot, tagged by boot.
{
	head -1 "$(ls "$EXP"/metrics-*.csv | head -1)" | sed 's/^/boot,/'
	for f in "$EXP"/metrics-*.csv; do
		tag=$(basename "$f" .csv); tag=${tag#metrics-}
		tail -n +2 "$f" | sed "s/^/$tag,/"
	done
} >"$EXP/metrics.csv" 2>/dev/null || true

python3 "$FU_REPO/scripts/analyze.py" "$EXP"
fu_log "results: $EXP/summary.md"
