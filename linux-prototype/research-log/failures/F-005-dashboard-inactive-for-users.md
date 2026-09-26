# F-005 — desktop dashboard showed "fuhrerd not running"

**Symptom:** the desktop's dashboard terminal (running as user `fuhrer`) printed `Runtime: INACTIVE` although fuhrerd was running.
**Reproduction:** desktop profile, `watch fuhrer status` as non-root.
**Root cause:** liveness check `kill(pid, 0)` fails with `EPERM` for an unprivileged caller probing a root process; the code treated any error as "not running".
**Fix:** treat `EPERM` as alive.
**Regression test:** manual (screenshot `docs/images/desktop-dashboard.png` shows the pre-fix state); a non-root check could be added to `fuhrer-selftest`.
**Lesson:** distinguish "no such process" (ESRCH) from "not allowed" (EPERM).
