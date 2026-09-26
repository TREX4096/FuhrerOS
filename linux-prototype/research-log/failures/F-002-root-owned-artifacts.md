# F-002 — build artifacts owned by root; SSH key unusable

**Symptom:** after the first successful build, `out/` files (including `ssh/id_ed25519`) were owned by root.
**Root cause:** the builder container runs as root and writes through a bind mount.
**Fix:** `build.sh` passes `HOST_UID/HOST_GID`; `mkimage.sh` chowns `/out` at the end and sets the key to 0600.
**Regression test:** `test.sh` uses the key for every SSH step.
**Lesson:** container-built artifacts must be handed back to the invoking user explicitly.
