# F-106 — A multi-threaded program never finished exiting

**Symptom:** After transition printed its summary, the benchmark script stalled.
**Reproduction:** transition (threads blocked in accept/recv, others spinning)
**Expected:** process exit terminates all threads
**Observed:** threads blocked in syscalls and threads spinning in ring 3 kept running
**Root cause:** proc_exit only set the killed flag; blocked threads were not woken and running threads never re-entered the kernel.
**Fix:** proc_exit calls proc_kill (wakes blocked threads); proc_check_killed runs on every interrupt return to ring 3.
**Regression test:** All suites run to completion; kill works on tight loops.
**Lesson:** Exit must actively stop every thread.
