# Research design

## Question (§14)
> Can a small OS dynamically choose scheduling/resource-management policies
> based on runtime workload characteristics and improve performance or
> responsiveness across heterogeneous workloads?

## Research questions → instruments
| RQ | question | instrument |
|---|---|---|
| RQ1 | Do runtime characteristics select beneficial policies? | per-task classes (`/proc/tasks`, `/proc/adapt`) vs. per-scenario winners in `schedbench`/`iobench` |
| RQ2 | Does adaptive scheduling beat fixed scheduling on heterogeneous workloads? | `sched` suite: B0 round-robin, B1 priority, B2 low-latency, B3 adaptive on *mixed* and *batch* scenarios |
| RQ3 | Profiling overhead? | `profiler_overhead_ns` and `sched_overhead_ns` in `/proc/sched`; A2 (profiling off) vs A4 |
| RQ4 | How fast must it react? | `transition` suite (detection delay per phase); A6 (window 25/100/500 ms), A7 (hysteresis 1/2/5) |
| RQ5 | Adaptive caching without excess memory? | `cache` suite: 5 policies × 4 workloads at a fixed 4 MiB cache; hit rate, throughput, latency, cache CPU |
| RQ6 | Better responsiveness without hurting batch throughput? | wake-up/response P99 vs batch jobs/s in the same runs |
| RQ7 | Implementable without excessive complexity? | size of the mechanism: `sched/adaptive.cpp` + `sched/profiler.c` + cache policy code (reported in EXPLANATION.md) |

## Baselines (§37) and ablations (§38)
B0 round-robin, B1 static priority, B2 manually selected low-latency,
B3 adaptive — all inside FuhrerOS, same boot, same VM.
A1 controller off (priority), A2 profiling off, A3 profiling without
switching (round-robin with the profiler on), A4 adaptive, A5 adaptive
scheduler + adaptive cache (cache suite), A6 window 25/500 ms, A7
hysteresis 1/5.

## Workloads
- `schedbench` *mixed*: 3 CPU-bound workers (prime counting), 1 I/O-bound
  worker (random 4 KiB reads with 1 ms think time), 1 interactive probe
  (sleep 10 ms, ~0.2 ms of work) measuring wake-up latency and response.
- *batch*: 4 CPU-bound workers + probe.
- `iobench`: seq, random, hotset (80 % in 2 MiB), scan (hotset + large scan)
  on a 64 MiB file with a 4 MiB cache.
- `transition`: CPU → random I/O → sequential I/O → network (loopback TCP)
  → interactive, 8 s each, sampled every 250 ms.

## Methodology
- Every experiment records host, QEMU, acceleration, vCPUs offered and used,
  memory, commit (`config.json`), the exact suite script, and the raw serial
  log; `results.jsonl` holds every measured record.
- Order of policies is rotated across repetitions (drift control).
- Nothing is reported that was not measured; missing values are NOT RUN.

## Threats
Nested virtualisation (Windows → WSL2 → KVM); single-CPU kernel; small
repetition counts; synthetic workloads; the same author wrote the policy and
the benchmarks (risk of tuning to the benchmark — mitigated by keeping
policy parameters fixed since before the runs, D-109).
