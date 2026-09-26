# F-109 — Interactive and network phases detected as IDLE

**Symptom:** Transition pilot run: the interactive and network phases never matched their expected class.
**Reproduction:** transition with the first version of profiler_system_class
**Expected:** INTERACTIVE / IO_BOUND
**Observed:** IDLE
**Root cause:** The system class was a CPU-time-weighted vote with an IDLE floor of 5 ms of CPU per window; tasks that mostly block barely use CPU.
**Fix:** Weight by activity: CPU ms + wake-ups + I/O operations (D-110).
**Regression test:** Transition runs detect all five phases.
**Lesson:** Choose features that describe the behaviour being classified, not only CPU use.
