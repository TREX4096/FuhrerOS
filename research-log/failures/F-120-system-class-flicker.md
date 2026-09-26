# F-120 — The system-level class flickered on every profiler window

**Symptom:** The new Control Center reported "Transitions since boot: 173" after 84 s of light use (typing a few commands). The top bar alternated between IDLE and INTERACTIVE.
**Reproduction:** Boot the desktop, type in a terminal, open the Control Center (before this fix).
**Expected:** A stable class that changes when the workload changes.
**Observed:** The class changed on almost every 100 ms window.
**Root cause:** Per-task classes use hysteresis (a change needs `hysteresis` agreeing windows), but the system-level class was recomputed from scratch every window. Short bursts of typing crossed the activity floor and fell below it again.
**Fix:** The same hysteresis is applied to the system-level class (`profiler_stable_class()`). The desktop, notifications and Control Center show the stable class. `/proc/adapt` still reports the instantaneous `system_class`, which the transition experiments (E-103..E-119) measured, next to the new `stable_class`.
**Regression test:** 3 transitions in 39 s of the same use after the fix (Control Center screenshot, docs/images/control-center.png).
**Lesson:** A value shown to people needs the same smoothing as the value the kernel acts on; a flickering indicator makes the adaptive layer look broken even when it works.
