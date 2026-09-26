# F-105 — Duplicated output lines

**Symptom:** Every TL line of transition appeared twice in the serial log.
**Reproduction:** transition (a multi-threaded program that prints)
**Expected:** each printf once
**Observed:** lines duplicated
**Root cause:** The libfu stdout buffer was shared by threads without a lock: sleep_ms() flushes, racing with another thread's newline flush.
**Fix:** A yielding spinlock around the stdout buffer.
**Regression test:** Transition output in the E-1xx runs has no duplicates.
**Lesson:** Once threads exist, shared state in the user runtime needs locking.
