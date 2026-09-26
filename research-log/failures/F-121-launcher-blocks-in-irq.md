# F-121 — Launching an app from the launcher froze the desktop

**Symptom:** After typing "sett" + Enter in the new launcher, the screen stopped updating; later key presses had no effect.
**Reproduction:** `./scripts/screenshot.sh build/ui4 20 meta_l s e t t ret ...` before the fix.
**Expected:** Settings opens.
**Observed:** The desktop hung.
**Root cause:** Keyboard input is handled in interrupt context. The launcher spawned the process (ELF load through the VFS) and, when opening, walked `/home` on the FFS0 disk. Both can sleep, and a sleep inside an interrupt handler deadlocks. The old start menu also spawned from interrupt context and only worked because the binary happened to be in memory (tarfs).
**Fix:** Input handlers only queue blocking work (`defer()`): process launch, directory indexing, scheduler/cache policy switches, opening a path. The compositor thread runs the queue in normal thread context. The file index is built privately and published in one step, so the interrupt-side search never sees a half-written entry.
**Regression test:** Launcher -> Settings, Ctrl+Alt+T, `notify` and Super+F in one screenshot session (docs/images/*.png).
**Lesson:** Anything reachable from an input handler must be audited for sleeping. "It worked before" can mean the slow path was never taken.
