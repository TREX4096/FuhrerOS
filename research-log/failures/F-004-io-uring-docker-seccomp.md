# F-004 — io_uring unavailable inside the Docker build container

**Symptom:** in the builder container `fuhrer-bench --backend batched` reported all operations on the *normal* backend.
**Root cause:** Docker's default seccomp profile blocks `io_uring_setup`. libfuhrer correctly fell back to synchronous I/O (designed behaviour), so the unit tests passed but did not exercise io_uring.
**Fix:** none needed for correctness; coverage gap closed by running the same tests **inside the VM** (`fuhrer-selftest` → `unit-libfuhrer`, and the `io_uring` check which fails unless batched ops are non-zero).
**Lesson:** a passing test in a sandbox may be exercising a fallback path; assert on which path ran.
