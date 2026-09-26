"""Writes research-log/experiments/E-1xx.md from experiments/*/config.json plus
hand-written interpretation (NOTES). Numbers quoted in NOTES are copied from
the run's summary.md; nothing here is computed or estimated."""
import glob, json, os

os.chdir(os.path.join(os.path.dirname(__file__), ".."))
OUT = "research-log/experiments"
os.makedirs(OUT, exist_ok=True)

Q = {
    "sched": ("RQ2/RQ6: does the adaptive scheduler (B3) improve interactive latency against fixed policies "
              "(B0 round-robin, B1 priority, B2 low-latency) without hurting batch throughput?",
              "B3 matches B2 on interactive latency and keeps B0's batch throughput in the mixed scenario.",
              "schedbench: `mixed` = 3 CPU workers + 1 random-read I/O worker + interactive probe (sleep 10 ms, "
              "~0.2 ms work); `batch` = 4 CPU workers + probe. 10 s per run, policy order rotated per repetition.",
              "probe wake-up / dispatch / response latency percentiles, batch jobs/s, I/O ops/s, Jain fairness, "
              "context switches/s, scheduler overhead ns/s, kernel-assigned classes"),
    "cache": ("RQ5: does the adaptive buffer cache improve storage throughput over fixed policies?",
              "Adaptive matches read-ahead on sequential reads and LRU on re-use workloads.",
              "iobench on a 64 MiB file through a 1024-block (4 MiB) cache: seq, random, hotset (80 % in 2 MiB), "
              "scan (hotset + sequential scan). 6 s per run, 2 repetitions, reversed order in the second.",
              "MB/s, hit rate, read latency p99, cache CPU, read-ahead issued/used"),
    "transition": ("RQ4 (NEW_EXPLANATION §36): how quickly does the kernel's classifier follow a change of workload?",
                   "The system class matches each phase within a few profiler windows (< 1 s).",
                   "transition: phases cpu -> random-io -> sequential-io -> network (loopback TCP) -> interactive, "
                   "8 s each, sampled every 250 ms; once under adaptive (sched+cache), once under round_robin+lru.",
                   "per-sample detected class, confidence, policies, ops; detection delay per phase"),
    "ablation": ("RQ1/RQ3 and NEW_EXPLANATION §38: which parts of the adaptive scheduler produce its effect, and "
                 "how sensitive is it to window and hysteresis?",
                 "Latency gains need both profiling and class-based parameters; longer windows / more hysteresis "
                 "react slower and raise tail latency.",
                 "schedbench mixed scenario, 3 repetitions per variant: A1 priority policy (controller off), A2 adaptive "
                 "with profiling off, A3 round-robin with profiling on, A4 adaptive, A5 adaptive + adaptive cache "
                 "(from E-120), A6 window 25/500 ms, A7 hysteresis 1/5.",
                 "as for sched"),
}

NOTES = {
    "E-101": ("Pre-fix run. Mixed scenario: B3 wake p50 964 µs vs B0 30,040 µs; I/O ops/s 488 vs 21; batch "
              "jobs/s about 13 % below B0. Batch scenario: B3 wake p99 5,742 µs vs B2 2,383 µs. The kernel reported "
              "system class IDLE for all runs.",
              "Superseded. It exposed F-109 (system class IDLE for blocking workloads). Its wake latencies are also "
              "affected by F-117 (tick phase) and F-118 (idle wake-up waits for the tick).",
              "E-105"),
    "E-102": ("Pre-fix run. All five policies read sequentially at 3.83-3.88 MB/s; read-ahead never triggered. "
              "Hotset: LRU best (17.74 MB/s), FIFO worst (11.64).",
              "Superseded. It exposed F-114 (metadata reads hid the sequential stream) and F-113 (cache CPU "
              "included disk waits). Every miss took about 1 ms because of F-118, which was not yet known.",
              "E-106"),
    "E-103": ("Detection within about 300 ms for every phase except the network phase under adaptive (5,689 ms).",
              "Superseded. The 5.7 s was later shown to be a starved sampler thread (F-115), not slow "
              "classification.", "E-107"),
    "E-104": ("A2 (profiling off) wake p50 12,023 µs vs A4 (adaptive) about 0.97 ms; A1/A3 about 30 ms.",
              "Superseded (wake latency affected by F-117/F-118; no A5).", "E-108"),
    "E-105": ("After F-109/F-113/F-114 fixes. Mixed: B2 and B3 wake p50 43-44 µs vs B0/B1 about 20 ms. B3 wake p99 "
              "3,268 µs (CV 97 %) vs B2 1,132 µs; B3 scheduler overhead 311,582 ns/s vs B2 126,314. Batch jobs/s "
              "equal within noise.",
              "Superseded. Its wake-latency level (about 44 µs) differs from E-108 (about 975 µs) for the same "
              "configuration, which led to F-117; B3's tail variance is consistent with F-115.", "E-109"),
    "E-106": ("Read-ahead now triggers: readahead policy 5.86 MB/s vs LRU 3.87 MB/s sequential, hit rate 914 vs 738 "
              "per mille, but 110k prefetches issued vs 6k used and 0.94 s cache CPU per run. Adaptive gained "
              "nothing (3.72 MB/s).",
              "Superseded. It exposed F-116 (read-ahead evicted its own prefetches).", "E-110"),
    "E-107": ("Same as E-103: network-phase detection 4,378 ms under adaptive, about 300 ms otherwise. The raw "
              "timeline has a 4.4 s gap in samples at the start of the network phase.",
              "Superseded. The gap led to F-115 (aging did not lift IDLE-class tasks above IO_BOUND tasks).",
              "E-111"),
    "E-108": ("A4 wake p50 975 µs (E-105 had 44 µs for the same configuration); A6-window500 and A7-hyst5 had "
              "much higher p99 (16,001 / 13,995 µs) than window25 / hyst1 (1,381 / 1,075 µs).",
              "Superseded. Ran on the pre-F-115/F-116 binary while those fixes were already in the working tree "
              "(the config says `-dirty`, but the ISO was built before the edits). Exposed F-117.", "E-112"),
    "E-109": ("First run with dispatch latency. Mixed: dispatch p50 B2 7 µs, B3 6 µs, B0/B1 about 29 ms; dispatch "
              "p99 B2 125 µs, B3 154 µs. Batch jobs/s within 5 % across policies.",
              "Superseded by E-113. Its I/O numbers are limited by F-118. An earlier attempt at this run was "
              "aborted by hand before any result was written, when F-117 was found; it was deleted.", "E-113"),
    "E-110": ("After F-116: readahead policy 99.9 % hit rate, read p99 78 µs, 390k-462k reads in 6 s. Its "
              "`mb_per_s` column is wrong (64-bit overflow in iobench), and every miss for other policies took "
              "about 1 ms.",
              "Superseded. Exposed F-118 (idle wake-up waited for the tick) and the iobench overflow.", "E-114"),
    "E-111": ("After F-115: detection 280-410 ms in every phase for both runs (network under adaptive 396 ms, "
              "was 4,378 ms).",
              "Confirms F-115. Superseded only because F-118 changed I/O timing.", "E-115"),
    "E-112": ("Same pattern as E-108 with dispatch latency: A2 dispatch p50 11,036 µs vs A4 6 µs; window500 / hyst5 "
              "dispatch p99 13,025 / 12,030 µs vs window25 / hyst1 49 / 52 µs.",
              "Superseded (F-118; the ablation still had no A5, and A1-A4 ran with the adaptive cache, which is the "
              "boot default).", "E-116, E-120"),
    "E-113": ("After F-118. Mixed: dispatch p50 B2 7 µs, B3 7 µs, B0/B1 about 29 ms; dispatch p99 B2 109 µs, B3 "
              "135 µs; batch jobs/s B0 974,371 vs B3 952,167 (x100); I/O ops/s B2 510, B3 501, B0 21. Batch "
              "scenario: all policies 979k-988k jobs/s (x100).",
              "Superseded only because F-119 changed the (default) adaptive cache under all policies; the "
              "scheduler code is unchanged in E-117.", "E-117"),
    "E-114": ("After F-118: LRU sequential 19.47 MB/s (was 3.87), random 19.92 MB/s; readahead sequential "
              "307.47 MB/s, p99 66 µs; adaptive sequential 21.62 MB/s with 23 µs cache CPU per read.",
              "Superseded. Exposed F-119 (evict-behind evicted metadata; readahead_used > readahead_issued).",
              "E-118"),
    "E-115": ("Detection 282-397 ms under adaptive, 282-583 ms under round_robin+lru, every phase.",
              "Superseded only because F-119 changed the adaptive cache.", "E-119"),
    "E-116": ("Old ablation layout (no A5; A1-A4 with the default adaptive cache). A2 dispatch p50 12,005 µs vs "
              "A4 6 µs; dispatch p99 A6-window500 12,028 µs and A7-hyst5 21,027 µs vs A6-window25 43 µs and "
              "A7-hyst1 44 µs.",
              "Superseded by the A5 layout.", "E-120"),
    "E-117": ("Mixed: dispatch p50 B3 6 µs, B2 7 µs, B0/B1 29,062 µs; dispatch p99 B3 84 µs, B2 104 µs.",
              "Superseded: ran with the aging defect later found by M17 (F-122) and the write-through disk "
              "(F-123).", "E-129"),
    "E-118": ("Adaptive sequential 311.58 MB/s vs read-ahead 321.13 and LRU 19.67; hotset adaptive 61.55 vs LRU "
              "75.32.", "Superseded: F-123 (write-through disk) was still present.", "E-130"),
    "E-119": ("Detection 284-401 ms under adaptive, 286-301 ms under round_robin+lru.",
              "Superseded (F-122, F-123 fixed later).", "E-131"),
    "E-120": ("A4 dispatch p99 56 µs, A5 71 µs, A6-window500 14,042 µs, A7-hyst5 11,990 µs; A2 dispatch p50 "
              "11,205 µs.", "Superseded (F-122).", "E-132"),
}

TYPES = {"micro": ("NEW_EXPLANATION §35: reproducible CPU, memory, storage, network and desktop micro-benchmarks.",
                   "No hypothesis: a baseline of what the system does.",
                   "fubench cpu/mem/storage/net x3, then fubench desktop once (it starts the compositor).",
                   "time, rate or latency per benchmark (units in summary.md); NOT RUN where impossible"),
         "stress": ("M17 / §35 mixed workload / UI_SUGGESTION §41: does the adaptive scheduler keep the desktop "
                    "responsive while browser-like, build-like, file and network work run at once?",
                    "Adaptive keeps the interactive probe near low-latency levels without starving the rest.",
                    "deskstress 10 s x 3 repetitions x 4 policies: HTTP client+server, two CPU-bound build-proxy "
                    "workers, 8 MiB file copy loop, TCP stream, interactive probe; order rotated.",
                    "probe dispatch/wake latency, per-component throughput, context switches, thread classes"),
         "wdebug": ("Diagnostic: why are FFS0 writes slow (F-123)?", "-",
                    "fubench storage with /proc/bcache, /proc/blk, /proc/sched before/after; E-126 with QEMU "
                    "cache=unsafe, E-127 with cache=none, E-128 after the FLUSH fix.",
                    "per-direction device latency, CPU busy/idle")}
Q.update(TYPES)

NOTES.update({
    "E-121": ("seq_write 1.82 MB/s, seq_read 366.09 MB/s, rand_write 842 ops/s, small files 230/s; "
              "UDP: 20000 sent, 32 received (unpaced sender).",
              "Superseded. Exposed F-123 and the unpaced UDP benchmark.", "E-133"),
    "E-122": ("Adaptive: probe dispatch p50 49,183 µs, file copy 0.06 MB/s (low-latency: 10 µs, 1.18 MB/s).",
              "Superseded. Exposed F-122 (aging boost never expired). HTTP numbers unreliable (fixed port, "
              "connection failures under every policy).", "E-134, E-135"),
    "E-123": ("seq_write 1.88 MB/s; CPU busy 1.78 s vs idle 20.9 s; 10,562 writebacks.",
              "Diagnostic: the kernel waits on the device.", "E-124"),
    "E-124": ("29,535 requests, 39.0 s summed device time.", "Diagnostic.", "E-125"),
    "E-125": ("Device latency: reads 243 µs, writes 3,196 µs.", "Diagnostic: writes are slow in the device.",
              "E-126"),
    "E-126": ("QEMU cache=unsafe: writes 162 µs, rand_write 5,848 ops/s.",
              "Diagnostic: the host flushes every write (write-through device, F-123).", "E-127"),
    "E-127": ("QEMU cache=none: writes 3,243 µs.", "Diagnostic.", "E-128"),
    "E-128": ("After negotiating VIRTIO_BLK_F_FLUSH: writes 148 µs, seq_write 20.56 MB/s, rand_write 4,614 ops/s, "
              "small files 2,246/s, 6 flushes.", "Confirms the F-123 fix.", "E-133"),
    "E-134": ("After F-122: adaptive probe dispatch p50 27 µs (was 49,183). HTTP failures under several "
              "policies (161/90/482 failed connects).", "Superseded: fixed web port made HTTP unreliable.",
              "E-135"),
})


def record(d, final_notes=None):
    cfg = json.load(open(os.path.join(d, "config.json")))
    e, suite = cfg["experiment"], cfg["suite"]
    q, h, wl, metrics = Q[suite]
    result, interp, nxt = (final_notes or NOTES)[e]
    rel = os.path.relpath(d, OUT).replace("\\", "/")
    status = "final" if final_notes else "superseded"
    return f"""# {e} — `{suite}` ({status})

- **Question:** {q}
- **Hypothesis:** {h}
- **Host:** {cfg['host']}; {cfg['host_cpu']} ({cfg['host_note']})
- **Guest:** FuhrerOS, {cfg['vcpus']} vCPU offered / {cfg['vcpus_used_by_kernel']} used, {cfg['memory_mb']} MB
- **QEMU:** {cfg['qemu']}, accel {cfg['accel'].upper()}; {cfg['disk']}; {cfg['network']}
- **Kernel:** {cfg['kernel']}, commit `{cfg['commit']}`, {cfg['date']}
- **Workload:** {wl}
- **Policy / configuration:** `{cfg['suite_script']}` (copy in the run directory)
- **Metrics:** {metrics}
- **Result:** {result}
- **Variance:** see CV% columns in [summary.md]({rel}/summary.md)
- **Interpretation:** {interp}
- **Threats:** nested virtualisation (Windows → WSL2 → KVM); one CPU used; few repetitions; synthetic workloads.
- **Next experiment:** {nxt}
- **Raw data:** [{os.path.basename(d)}]({rel}/) (serial.log, results.jsonl, summary.md)
"""


if __name__ == "__main__":
    import sys
    final = {}
    if len(sys.argv) > 1:
        final = json.load(open(sys.argv[1], encoding="utf-8"))
    for d in sorted(glob.glob("experiments/E-1*")):
        e = os.path.basename(d)[:5]
        if e in NOTES or e in final:
            txt = record(d, final if e in final else None)
            open(os.path.join(OUT, e + ".md"), "w", encoding="utf-8", newline="\n").write(txt)
            print("wrote", e)
