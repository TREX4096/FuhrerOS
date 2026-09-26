# F-103 — 20 GiB disk image fully allocated on NTFS

**Symptom:** du reported 20G for build/disk.img on /mnt/d.
**Reproduction:** make disk with the checkout on D:
**Expected:** a sparse file of a few MiB
**Observed:** 20 GiB allocated on the Windows drive
**Root cause:** drvfs/NTFS does not keep the file sparse.
**Fix:** Build outputs go to ~/.cache/fuhreros/build when the repository is on /mnt/* (D-106); the 20 GiB file was deleted.
**Regression test:** du -h of the image after every build (5.6 MiB).
**Lesson:** Measure the disk usage of generated images.
