# UI design notes (§25)

Goal: "Windows familiarity + GNOME simplicity + KDE configurability + modern
tiling", without copying any implementation.

| idea | where it comes from | FuhrerOS form |
|---|---|---|
| taskbar with launcher button, window buttons, tray + clock | Windows | bottom panel; the tray shows the live scheduler policy and system workload |
| Super opens a launcher; Super+arrows snap | Windows / GNOME | launcher opens on Super *release* (a chord like Super+T must not open it, F-111) |
| workspaces as a first-class concept | GNOME / KDE | four workspaces on the panel, Super+1…4 |
| overview of open windows | GNOME Activities / macOS Mission Control | Super+O and 3-finger swipe up; live thumbnails |
| optional automatic tiling | Hyprland / COSMIC | Super+T: master/stack layout per workspace |
| configurable behaviour | KDE | Settings app (scheduler/profiler parameters, theme) |
| lock screen with a clock | all | Super+L |

Visual language: dark neutral surfaces, one accent colour (#4F9DFF), rounded
buttons, drop shadows, an 8×16 bitmap font. Research state is part of the UI,
not hidden in a debug shell (§32–33).
