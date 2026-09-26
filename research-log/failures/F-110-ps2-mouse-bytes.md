# F-110 — Keyboard IRQ would consume mouse bytes

**Symptom:** Found in code review before the first boot.
**Reproduction:** n/a
**Expected:** mouse bytes reach the mouse decoder
**Observed:** the keyboard handler read and discarded them
**Root cause:** Both devices share the i8042 output buffer and the keyboard handler read every byte.
**Fix:** A single drain routine dispatches each byte by the AUX status bit.
**Regression test:** Desktop pointer and keyboard work together (screenshots).
**Lesson:** Shared hardware buffers need a single consumer.
