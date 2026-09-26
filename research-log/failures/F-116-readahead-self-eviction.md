# F-116 — Read-ahead evicted its own prefetched blocks

**Symptom:** After F-114, read-ahead worked but was wasteful. The `readahead` policy issued about 110,000 prefetches for a 64 MiB (16,384-block) file and used about 6,000 of them, costing about 0.94 s of cache CPU per 1 s run. The adaptive policy gained no sequential throughput (3.72 vs 3.87 MB/s for LRU).
**Reproduction:** `./scripts/experiment.sh cache` (E-106).
**Expected:** Each block is prefetched about once and then used.
**Observed:** Prefetch counts about 7x the file size in blocks; evictions about the same.
**Root cause:** Prefetched blocks were inserted at the cold end of the LRU list, which is where `victim()` evicts from. Each read-ahead batch evicted the previous, not-yet-used batch. In adaptive mode, evict-behind also moved consumed blocks to that end, mixing them with unused prefetches.
**Fix:** Prefetched blocks are inserted at the hot end. Consumed stream blocks still go cold (evict-behind), so they are evicted first.
**Regression test:** `readahead_used / readahead_issued` in the cache suite (post-fix run in docs/experiments.md).
**Lesson:** Where a new block enters the replacement list is part of the policy. Check the efficiency counters (issued vs used), not only throughput.
