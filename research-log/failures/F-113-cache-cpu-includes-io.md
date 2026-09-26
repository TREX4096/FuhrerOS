# F-113 — Cache CPU time included disk waits

**Symptom:** iobench reported about 1 s of cache CPU time for a 1 s run.
**Reproduction:** iobench -t 1
**Expected:** CPU time of cache bookkeeping
**Observed:** wall time of reads, including I/O waits
**Root cause:** TSC cycles were accumulated across blk_rw sleeps.
**Fix:** Subtract the time spent waiting for the disk.
**Regression test:** cache_cpu_us values in the cache suite.
**Lesson:** Separate CPU time from wall time in every metric.
