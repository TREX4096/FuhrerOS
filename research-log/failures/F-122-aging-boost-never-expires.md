# F-122 — The adaptive scheduler's aging boost never expired

**Symptom:** In the M17 desktop-stress run (E-122) the adaptive policy was the worst of the four:
- probe dispatch p50 49,183 µs (low-latency: 10 µs);
- file copy 0.06 MB/s;
- almost no HTTP requests served.

The simpler mixed scenario (E-117) had shown adaptive close to low-latency.
**Reproduction:** `./scripts/experiment.sh stress` at `fceac52`.
**Expected:** Aging lifts a starved task for one quantum, then the task returns to its class priority.
**Observed:** Starved CPU-bound tasks stayed at priority 4, with their 30 ms CPU_BOUND quantum, alongside the interactive probe.
**Root cause:**
- In `Adaptive::tick()`, an expired quantum was refilled (`ticks_left = quantum_ticks`) before `requeue()` ran.
- `requeue()` only re-applies the class parameters when `ticks_left == 0`, so for a task that never blocks, `setup()` was never called again and a boost lasted until the task blocked.
- F-115 raised the boost from 12 to 4, which made this old defect harmful: permanently boosted CPU hogs share the interactive level with 30 ms slices.
- E-117 had too few threads to push workers past the 100 ms aging threshold; the stress mix does.

**Fix:**
- `tick()` calls `setup()` when a quantum ends, which ends any boost.
- A boosted task gets an interactive-sized (3 ms) quantum.

**Regression test:** the stress suite. Adaptive dispatch p50/p99 must be comparable to the low-latency policy (post-fix campaign in docs/experiments.md). `deskstress` also prints each thread's priority and class mid-run.
**Lesson:**
- A fix (F-115) can turn a latent bug harmful. Every scheduler change needs the full campaign, including a workload with more runnable threads than the scenario that motivated it.
- Temporary state (a boost) must have an explicit end.
