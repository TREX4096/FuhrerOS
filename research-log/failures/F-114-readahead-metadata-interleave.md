# F-114 — Read-ahead never triggered on sequential file reads

**Symptom:** In E-102 every cache policy, including readahead and adaptive, read a sequential file at about 3.9 MB/s. `readahead_issued` stayed at 0.
**Reproduction:** `./scripts/experiment.sh cache` at commit 3facef8 (pre-fix E-102).
**Expected:** Sequential detection after a few consecutive blocks, then prefetching.
**Observed:** The access stream seen by the detector alternated data blocks with inode/indirect-block reads, so consecutive-block runs never formed.
**Root cause:** FFS0 read file metadata through the same `bread()` path as file data, and every access updated the sequential-stream detector.
**Fix:** Added `bread_meta()`. Metadata lookups bypass `account_access`; only file data (`VT_FILE`) feeds the detector.
**Regression test:** `bcache.policies` self-test, and `readahead_issued` > 0 in the post-fix cache suite (E-106).
**Lesson:** Keep workload detectors on the stream you want to classify; unrelated accesses hide the pattern.
