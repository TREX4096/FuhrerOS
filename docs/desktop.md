# Desktop

Booting the default menu entry starts `/bin/desktop`, which switches the
kernel into desktop mode and launches the Control Center and a Terminal.
The design and its decision records are in [ui-design.md](ui-design.md).

## Shell

- **Top bar:**
  - logo (opens the launcher);
  - workspaces with names, plus a dot where windows are;
  - layout mode (click to cycle);
  - the focused window's title;
  - a status group: adaptive policy and workload class, CPU, RAM and
    network rates (click for the Control Center);
  - the clock.
- **Launcher / command palette** (Super):
  - searches apps, commands and files in `/home`;
  - `>` restricts the search to commands;
  - `workspace N`, `rename NAME` and `open PATH` take arguments.
  - The commands are:
    - lock, shut down, reboot;
    - show desktop, overview;
    - layouts;
    - dark/light theme, wallpaper, accent;
    - scheduler and cache policies;
    - add/remove workspace;
    - adaptive notifications.
- **Alt+Tab:** a switcher with live thumbnails while Alt is held.
- **Overview** (Super+Tab, 3-finger up): workspace strip + window
  thumbnails.
- **Notifications:** top right, auto-dismiss after 6 s, click to dismiss.
  Send one with `notify TITLE [TEXT]`.
- **Lock screen** (Super+L): logo and clock; any key unlocks (there are no
  user accounts yet).

## Window management

Each workspace is in one of three modes:

- **floating**: move by dragging the title, resize from the corner, snap
  halves, drag to the top edge to maximise;
- **tiling**: master/stack with an adjustable split, rotation, swapping,
  and per-window float;
- **focus**: one maximised window.

| shortcut | action |
|---|---|
| Super (tap) | launcher / command palette |
| Alt+Tab (+Shift) | window switcher |
| Super+Tab, Super+O | overview |
| Super+T | layout: floating → tiling → focus |
| Super+←/→ | snap (floating) or focus neighbour (tiling) |
| Super+Shift+←/→ | swap tiles |
| Super+[ / ] | shrink / grow master |
| Super+R | rotate tiling orientation |
| Super+Space | float / re-tile window |
| Super+↑/↓ | maximise / restore-minimise |
| Super+D | show desktop |
| Super+L | lock screen |
| Super+F | Fuhrer Control Center |
| Super+E | files |
| Super+1…9 / Super+Shift+1…9 | workspace / move window there and follow |
| Super+Ctrl+←/→ | previous / next workspace |
| Super+Shift+Q | close window |
| Ctrl+Alt+T | terminal |

## Applications (`user/apps/`)

All apps follow the desktop theme (dark/light, accent) through `fu_theme`.

| app | what it does |
|---|---|
| `term` | terminal emulator: runs `sh -i` over pipes, scrollback, ANSI colours (always dark) |
| `files [DIR]` | file manager: browse, open in editor, new folder, delete |
| `control` | **Fuhrer Control Center**: the adaptive kernel made visible (below) |
| `monitor` | CPU/memory history, task list with scheduling class, kill |
| `web` | FuhrerWeb: HTTP page viewer (links clickable), not an engine (D-116) |
| `edit [FILE]` | text editor with save |
| `settings [SECTION]` | Appearance, Touchpad, Desktop, Kernel, Shortcuts, About |

## Fuhrer Control Center

- **Resources:** cards for CPU, memory, network down/up and storage
  read/write, plus a 60 s CPU graph.
- **Workload:** the stable system class with confidence and reason.
- **Scheduler:** the active policy and, under adaptive, the parameters the
  dominant class receives.
- **Cache:** the policy, the detected stream type and the hit rate.
- **Network:** it has a fixed policy, and the panel says so.
- **Transitions:** the last system-level transition (how long ago, from →
  to), the number since boot, and profiler overhead as a share of CPU time.
- **Controls:** buttons that switch the scheduler and cache policies live.
- **Tasks:** the task table coloured by class, and recent per-task
  adaptations.

The layout reflows to one column when the window is narrow (tiling).

## Measuring the desktop (UI_SUGGESTION §40)

`cat /proc/desktop` shows the mode, the theme, frame count, average and
maximum composition time, input-event→frame latency (average and maximum)
and the last workspace-switch latency. These are measured in the guest.
They have not yet been collected into an experiment: NOT RUN.
