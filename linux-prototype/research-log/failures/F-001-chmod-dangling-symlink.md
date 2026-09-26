# F-001 — image build aborted on chmod of a dangling symlink

**Symptom:** `mkimage.sh` failed: `chmod: /work/rootfs/etc/init.d/functions.sh: No such file or directory`.
**Reproduction:** first `./scripts/build.sh`.
**Expected:** mark our init script executable.
**Observed:** `chmod +x $ROOT/etc/init.d/*` also touched Alpine's `functions.sh`, an absolute symlink (`/lib/rc/sh/functions.sh`) that resolves against the *container* root, where it does not exist.
**Root cause:** glob over a directory we do not own + chmod follows symlinks.
**Fix:** chmod only files we install (`etc/init.d/fuhrerd`).
**Regression test:** every `build.sh` run.
**Lesson:** when assembling a chroot from outside, never follow symlinks; operate only on paths we created.
