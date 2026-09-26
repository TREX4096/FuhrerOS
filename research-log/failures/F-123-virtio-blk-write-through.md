# F-123 — Every disk write waited for a host flush (virtio-blk write-through)

**Symptom:** §35 micro-benchmarks (E-121):
- sequential write 1.82 MB/s, against 366 MB/s for sequential read;
- random write 842 ops/s;
- 230 small files/s.

**Reproduction:** `./scripts/experiment.sh micro`; diagnosed with the `wdebug` suite (E-123..E-128).
**Expected:** A 4 KiB virtio write to a host file with `cache=writeback` completes in roughly the time of a read.
**Observed:**
- The CPU was idle 92 % of the benchmark (busy 1.97 s, idle 19.5 s).
- Per-request device latency: 243 µs for reads, 3,196 µs for writes (E-125).
- With QEMU `cache=unsafe`, writes took 162 µs (E-126); with `cache=none`, 3,243 µs (E-127).

**Root cause:** The driver negotiated no device features. A virtio-blk device whose driver does not accept `VIRTIO_BLK_F_FLUSH` must behave write-through, so QEMU made every single 4 KiB write durable on the host before completing it.
**Fix:**
- Negotiate `VIRTIO_BLK_F_FLUSH`, which gives the device a write cache.
- Add a FLUSH request (`blk_flush`).
- `bsync()` flushes after writing back: always on explicit `sync()` / poweroff, and from the periodic flusher only when it wrote something.

**Result (E-128, same suite):**

| metric | before | after |
|---|---|---|
| sequential write | 1.82 MB/s | 20.56 MB/s |
| random write | 853 ops/s | 4,614 ops/s |
| small-file create | 256 files/s | 2,246 files/s |
| device write latency | 3.2 ms | 148 µs |

**Durability:**
- Before: every write was durable at completion.
- Now: writes are durable after the next `sync()` / flush, or within the flusher's 2 s period, as on other systems with write-back caches.
- FFS0 has no journal; a crash can lose up to 2 s of writes. This is recorded in docs/filesystem.md.

**Regression test:** `storage.seq_write` in the micro suite. `/proc/blk` shows `write_cache=1` and a nonzero `flushes`.
**Lesson:** Negotiated features change device semantics, not only speed. Split latency by direction before blaming the filesystem.
