# F-119 — Adaptive cache's evict-behind evicted the file's own metadata

**Symptom:** After F-116 and F-118, the adaptive cache read sequentially at 21.6 MB/s. That is barely above LRU (19.5 MB/s) and far below the read-ahead policy (307 MB/s). It cost 23 µs of cache CPU per read (LRU: 0.6 µs), and `readahead_used` (64,683) exceeded `readahead_issued` (32,458).
**Reproduction:** `./scripts/experiment.sh cache` at 689bf6c (E-114).
**Expected:** In the SEQUENTIAL class, adaptive behaves like read-ahead plus evict-behind.
**Observed:** Hit rate the same as LRU (755 vs 745 per mille); median read latency 141 µs (read-ahead: 6 µs).
**Root cause:** On every hit in adaptive-sequential mode, `get()` re-set `b->readahead = true`, and it did so for metadata blocks too. `touch()` then treated a block with that flag as a consumed stream block and moved it to the cold end (evict-behind). The inode and indirect blocks, needed by every read of the stream, were therefore evicted and re-read synchronously. The re-set flag also counted each hit as another "used prefetch".
**Fix:** `touch()` gets an explicit `data` argument; evict-behind applies only to file-data blocks. The read-ahead flag is cleared when a prefetched block is first used, so `readahead_used` counts each prefetch once.
**Regression test:** Adaptive vs readahead sequential throughput and `readahead_used <= readahead_issued` in the cache suite from E-117 on.
**Lesson:** The same mistake as F-114, on the eviction side. Every cache heuristic that acts on "the stream" must separate data from metadata.
