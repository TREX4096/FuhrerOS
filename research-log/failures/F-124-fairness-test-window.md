# F-124 — Scheduler fairness self-test too short for 30 ms quanta

**Symptom:** `TEST sched.concurrent.adaptive FAIL 81 switches in 300 ms, min share 200/1000`. It appeared in one boot after the F-122 fix; earlier boots with the same scheduler passed.
**Reproduction:** `./scripts/test.sh` (intermittent).
**Expected:** The self-test verifies that three equal CPU spinners all make progress (a fair share is 333‰; the test requires >200‰).
**Observed:** Under the adaptive policy the spinners become CPU_BOUND and get 30 ms quanta. A 300 ms window holds about 10 quanta, so one quantum more or less moves a share by about 100‰, and the minimum share landed exactly on the threshold.
**Root cause:** The measurement window was too short for the quantum length; the check was marginal by design. Since F-122, `setup()` applies class parameters at every quantum end, so CPU_BOUND quanta take effect sooner than before.
**Fix:** The concurrency test runs for 900 ms (about 30 quanta).
**Regression test:** `sched.concurrent.*` in every `./scripts/test.sh`.
**Lesson:** Test windows must be many times longer than the scheduling granularity they sample.
