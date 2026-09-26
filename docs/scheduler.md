# Scheduler

## Structure (NEW_EXPLANATION §13, §16)
- `sched/core.c` — tasks (threads), kernel stacks, the assembly context
  switch (`arch/x86_64/switch.S`), sleeping (sorted timer list), wait queues,
  mutexes, semaphores, preemption (quantum expiry or wake-up preemption,
  applied at the end of every interrupt), accounting and statistics.
- `sched/policy.hpp` — `Policy` base class with a 32-level priority run
  queue (bitmap lookup) and the `FUHRER_EXPORT_POLICY` macro that turns a
  C++ policy into a C `struct sched_policy`.
- `sched/policies.cpp` — the baselines; `sched/adaptive.cpp` — the adaptive
  policy; `sched/profiler.c` — per-task classification.

Interface (spec name → field): `scheduler_init`→`init`, `task_create`,
`task_block`, `task_wake`, `task_exit`, `scheduler_tick`→`tick`,
`scheduler_select_next`→`select_next`, plus `setup`, `requeue`, `remove`,
`classified`, `runnable`. Policies can be switched at run time; runnable
tasks migrate to the new policy's queue.

## Policies (baselines of §37)
| name | label | behaviour |
|---|---|---|
| `round_robin` | B0 | one FIFO queue, 10 ms quantum |
| `priority` | B1 | 32 static levels (user `nice`), 10 ms quantum, higher priority preempts |
| `low_latency` | B2 (manual) | 2 ms quantum, woken tasks go first and preempt |
| `adaptive` | B3 | class → priority / quantum / wake preemption (below) + aging |

Adaptive parameters (D-109): INTERACTIVE 4 / 3 ms / preempt; IO_BOUND
8 / 5 ms / preempt; MIXED·IDLE·UNKNOWN 12 / 8 ms; CPU_BOUND 20 / 30 ms.
Tasks waiting > 100 ms are lifted to priority 12.

## Profiler (D-108)
Per task and window (default 100 ms): CPU time, times scheduled, voluntary
blocks, involuntary preemptions, wake-ups, wake-up latency, I/O syscalls.
Rules: IDLE → IO_BOUND (≥ 4 I/O ops, bursts < 5 ms) → INTERACTIVE (≥ 2
blocks, bursts < 2 ms, < 30 % CPU) → CPU_BOUND (≥ 70 % CPU, or preempted
without blocking) → MIXED. Hysteresis 2 windows. Confidence = agreement of
the last 8 windows. Every change is logged (`/proc/adapt`, and `[ADAPT]`
lines on the console with the `adapt.log` boot flag).

System class (Control Center, experiments): the class with the most
*activity* (CPU ms + wake-ups + I/O ops) in the last window (D-110).

## Metrics (§17)
`/proc/sched`: context switches, preemptions, `sched_overhead_ns` (TSC
cycles inside `schedule()`), busy/idle time, profiler overhead. `schedbench`
measures wake-up latency and response P50/P95/P99, batch throughput, Jain
fairness, context switches/s and scheduler overhead per second.

## Control
`sched policy NAME`, `sched prio PID N`, `sched window MS`,
`sched hysteresis N`, `sched profiler on|off`, `sched quantum PCT`; the
Settings and Control Center apps expose the same controls.
