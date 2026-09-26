# F-111 — Super+T opened the launcher

**Symptom:** The screenshot after Super+T showed the launcher open.
**Reproduction:** scripts/screenshot.sh with the key sequence meta_l-t
**Expected:** only tiling toggles
**Observed:** the launcher toggled as well
**Root cause:** The launcher opened on the Super press event.
**Fix:** Open on Super release, and only if no other key was pressed meanwhile.
**Regression test:** Screenshot after Super+T without the menu.
**Lesson:** Model modifier chords on release, like the reference desktops.
