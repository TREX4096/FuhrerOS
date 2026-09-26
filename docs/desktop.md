# Desktop

Booting the default menu entry starts `/bin/desktop`, which switches the
kernel into desktop mode and launches the Control Center and a Terminal.

## Window management (§26)
Focus and stacking; move (drag the title bar), resize (drag the bottom-right
corner), minimise / maximise / restore (title-bar buttons, taskbar),
snap halves, drag to the top edge to maximise, four workspaces, an overview
with live thumbnails, and an optional master/stack **tiling** mode.

| shortcut | action |
|---|---|
| Super (tap) | launcher |
| Super+Tab, Alt+Tab (+Shift) | cycle windows |
| Super+Left / Right | snap left / right half |
| Super+Up / Down | maximise / minimise-restore |
| Super+D | show desktop |
| Super+L | lock screen |
| Super+O | overview |
| Super+T | toggle tiling |
| Super+1…4, Super+Shift+1…4 | switch workspace / move window |
| Super+E | files |
| Super+Q | close window |
| Ctrl+Alt+T | terminal |

## Applications (`user/apps/`)
| app | what it does |
|---|---|
| `term` | terminal emulator: runs `sh -i` over pipes, scrollback, ANSI colours |
| `files` | file manager: browse, open in editor, new folder, delete |
| `control` | **Fuhrer Control Center** — live research view (§33) |
| `monitor` | CPU/memory history, task list with scheduling class, kill |
| `web` | FuhrerWeb: HTTP page viewer (links clickable), not an engine (D-116) |
| `edit` | text editor with save |
| `settings` | wallpaper, scheduler policy, profiler window, hysteresis, shortcut list |

## Fuhrer Control Center (§33)
Resource bars (CPU, memory, disk, network) and a 60 s CPU graph; the
detected **system workload** with confidence and reason; the scheduling
policy (buttons switch it live) and the cache policy (buttons, with the
cache's I/O class and hit rate); network state; the task table coloured by
class; the latest class transitions. Screenshots: `docs/images/`.
