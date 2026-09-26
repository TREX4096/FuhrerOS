# F-125 — Timer self-test failed when the host descheduled the vCPU

**Symptom:** `TEST time.tsc.monotonic FAIL delay_us(1000) measured 2628216 ns`. It happened in one of three consecutive self-test boots of the same build; the other two passed.
**Reproduction:** `./scripts/test.sh` repeatedly while the Windows host is busy (intermittent).
**Expected:** A 1 ms busy-wait measures 1.0–1.5 ms.
**Observed:** 2.63 ms.
**Root cause:** Under nested virtualisation (Windows → WSL 2 → KVM) the host can preempt the vCPU during the wait. The TSC keeps counting, so the measured wait includes the host's time slice. This is not a FuhrerOS timer error; a single measurement cannot tell the two apart.
**Fix:** The test takes the minimum of 5 waits. Host preemption can only add time, so the minimum is the best estimate of the kernel's own accuracy.
**Regression test:** `time.tsc.monotonic` in every self-test run.
**Lesson:** In a VM, latency self-tests need a statistic that is robust to host noise (a minimum for "at least" checks, a median for distributions). This is the same threat to validity as in the experiments.
