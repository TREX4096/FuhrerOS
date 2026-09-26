# F-104 — Printing stalled the whole machine

**Symptom:** During the transition experiment the main thread of the benchmark, which only prints, used about 99 % CPU and network throughput collapsed.
**Reproduction:** netdbg suite (transition -d) under any policy
**Expected:** printing a line costs microseconds
**Observed:** every newline scrolled the framebuffer with memmove over 4 MB of write-combining video memory with interrupts disabled
**Root cause:** Reading write-combining framebuffer memory is extremely slow; scrolling did it once per line, inside console_write with interrupts off.
**Fix:** fbcon rewritten as a character-cell grid in RAM; rendering only writes video memory, once per write call.
**Regression test:** Experiment suites; the text console stays responsive.
**Lesson:** Never read from write-combining memory; batch rendering.
