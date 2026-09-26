# F-112 — Two task-exit races

**Symptom:** Found in code review.
**Reproduction:** n/a
**Expected:** safe teardown
**Observed:** potential zombie rescheduling and use-after-free
**Root cause:** (1) process teardown could sleep after the task was marked ZOMBIE, so a wake-up would make a zombie runnable; (2) a parent could reap and free a process whose last thread was still running on its kernel stack.
**Fix:** (1) teardown before becoming a zombie; (2) reap only when every thread has switched away (kernel stack freed).
**Regression test:** Process-heavy tests (usertest, benchmark suites) run without crashes.
**Lesson:** Lifecycle transitions must be ordered with the context switch.
