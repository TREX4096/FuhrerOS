# F-102 — libgcc built with the red zone

**Symptom:** x86_64-elf-gcc -print-multi-lib prints only '.' after the OSDev multilib patch.
**Reproduction:** toolchain/build-toolchain.sh, then x86_64-elf-gcc -print-multi-lib
**Expected:** a no-red-zone multilib
**Observed:** only the default multilib
**Root cause:** The sed that adds i386/t-x86_64-elf to config.gcc did not match its case label. UNKNOWN - REQUIRES VERIFICATION which exact pattern GCC 15.2 uses.
**Fix:** The kernel links no libgcc at all; the single 128/64 division was replaced (D-103). User space keeps libgcc (safe: interrupts use the kernel stack).
**Regression test:** scripts/check-kernel.sh rejects any undefined symbol, so a new libgcc dependency cannot slip in.
**Lesson:** Verify toolchain properties after building, not only that it builds.
