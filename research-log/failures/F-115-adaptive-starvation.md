# F-115 — Adaptive scheduler starved a sleeping thread for seconds

**Symptom:** In the transition experiment under the adaptive policy, "detection" of the network phase took 5.7 s (E-103) and 4.4 s (E-107), against about 0.3 s for every other phase and policy.
**Reproduction:** `./scripts/experiment.sh transition` before this fix.
**Expected:** A thread that sleeps 250 ms and prints one line runs within milliseconds of waking.
**Observed:** The timeline has no sample for 4.4 s after the network phase starts. The workers' throughput during the gap was normal (43,515 sends in 4.4 s), so only the sampler thread was not running. The first sample after the gap was already correct (IO_BOUND).
**Root cause:** Aging lifted tasks only if their priority was worse than 12, and only to 12. The sampler was classed IDLE (priority 12), so it was never lifted. The two workers kept their IO_BOUND class (priority 8) from the sequential-I/O phase and never blocked in `send()`, so every priority-12 task waited until the profiler reclassified them.
**Fix:** A task waiting more than 100 ms is lifted to priority 4 (the INTERACTIVE level) for one quantum, whatever its class.
**Regression test:** No `TL` gap larger than 600 ms in the transition suite (post-fix run in docs/experiments.md).
**Lesson:** Anti-starvation must lift a waiting task above *every* class, not only above the lowest one. A "detection latency" number can actually be a measurement thread that never got the CPU; check the raw timeline before interpreting it.
