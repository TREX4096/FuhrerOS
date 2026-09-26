# F-101 — Limine protocol header missing

**Symptom:** The first clone of limine-protocol from Codeberg contained only a README.
**Reproduction:** Clone https://codeberg.org/Limine/limine-protocol.git
**Expected:** include/limine.h present
**Observed:** README says the project moved to GitHub
**Root cause:** The Limine project moved back to GitHub (2026).
**Fix:** Clone both repositories from github.com/Limine-Bootloader; toolchain/get-limine.sh uses GitHub.
**Regression test:** The build fails loudly if limine.h is missing (it is vendored in kernel/include).
**Lesson:** Check where upstream actually lives before assuming a clone worked.
