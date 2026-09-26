# F-006 — `benchmark.sh --configs` silently ignored

**Symptom:** E-003 was launched with `--configs normal,specialized,adaptive` but ran all 8 configurations (lib-batched showed up in the log).
**Root cause:** `STATIC_CONFIGS` was assigned its default *after* the option-parsing loop, overwriting the parsed value.
**Fix:** default assigned before parsing.
**Impact:** none on validity. E-003 contains a superset of the requested configurations (more data, about 20 min longer).
**Lesson:** give defaults before parsing, and echo the effective configuration at start (the effective list is recorded in stdout.log through the per-run labels).
