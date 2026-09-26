# F-003 — knob unit tests read "168" after writing "16"

**Symptom:** `test_adaptive` knob checks failed (read_ahead_kb expected 16, got 168).
**Root cause:** `fu_write_file` opened without `O_TRUNC`. Real sysfs/procfs attributes ignore file offsets and length, so production was unaffected, but the fake sysfs tree in the tests is made of regular files.
**Fix:** open with `O_WRONLY|O_TRUNC` (what `echo >` does; safe on sysfs).
**Regression test:** `test_knobs` in `tests/unit/test_adaptive.c`.
**Lesson:** test doubles of kernel interfaces must model their write semantics, or the code should use the semantics that work on both.
