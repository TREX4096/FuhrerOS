# F-118 — A task woken on an idle CPU waited for the next timer tick

**Symptom:** Every buffer-cache miss took almost exactly 1 ms (LRU sequential read p50 = 1,000 µs, about 3.9 MB/s), in every run from E-102 to E-110. A virtio-blk 4 KiB read under KVM should take tens of µs. The `mb_per_s` of the fixed read-ahead policy looked implausible next to its read count, which exposed a second bug (below).
**Reproduction:** `iobench -p lru -w seq` before this fix.
**Expected:** A blocked reader runs as soon as its disk completion interrupt arrives.
**Observed:** It ran at the next 1 ms tick.
**Root cause:** `task_wake()` queued the task but only requested a reschedule through the policy's wake-preemption check, and every policy skips that check when the current task is the idle task. On an idle CPU, the completion IRQ returned to `hlt`, and only the next timer tick noticed the runnable task.
**Fix:** `task_wake()` sets `need_resched` whenever the current task is idle, so the switch happens when the waking interrupt exits.
**Also fixed:** `iobench` computed `bytes * 1e11` in 64 bits, which overflows above about 180 MB read. It now converts to KiB first. It also reports cache CPU per read, because total cache CPU grows with throughput.
**Regression test:** Cache-miss latency and throughput in the cache suite from E-113 on.
**Lesson:** A latency that sits exactly on the timer period is a symptom, not a property of the device. Check every wake-up path from interrupt context, including the idle case.
