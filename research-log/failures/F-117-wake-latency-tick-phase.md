# F-117 — Wake-up latency depended on the boot, not the policy

**Symptom:** For the same configuration (mixed scenario, adaptive policy, same binary), median wake latency was about 44 µs in E-105 but about 975 µs in E-108. Round-robin moved too (20 ms vs 30 ms). Every run within one boot agreed; the boots disagreed.
**Reproduction:** Compare `wake_p50_us` in E-105 (sched suite) with E-108 (ablation A4), or E-101 with E-105.
**Expected:** The same configuration gives the same latency within noise.
**Observed:** Two distinct levels, fixed for a whole boot.
**Root cause:** Sleepers are woken only by the periodic 1000 Hz LAPIC tick. The probe sleeps 10 ms starting right after a tick. The LAPIC tick is calibrated against the TSC once per boot. If the tick comes out slightly longer than 1 ms, the 10th tick falls just after the deadline (latency about 40 µs). If it comes out slightly shorter, the 10th tick falls just before, and the task waits one more tick (about 975 µs). The scheduler policy cannot affect this.
**Fix:** `schedbench` also records **dispatch latency**, the kernel-measured time from runnable to running (`SCHED_LAST_DISPATCH`), which is what a policy controls. Wake latency is kept but is only compared within one experiment, and summaries say so.
**Regression test:** `dispatch_p50_us` / `dispatch_p99_us` in every sched and ablation run from E-109 on.
**Lesson:** Check that a metric only measures the mechanism under test. Look for bimodal results that follow the boot rather than the configuration.
