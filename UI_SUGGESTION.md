# FuhrerOS UI/UX Design Specification

## 1. Purpose

This document defines the desired visual language, interaction model, desktop experience, touchpad behavior, accessibility expectations, and usability goals for FuhrerOS.

This is a **design specification and source of inspiration**, not a requirement to immediately implement every feature.

The implementation must respect the actual capabilities and maturity of the FuhrerOS kernel.

The guiding principle is:

> **Modern Arch-like minimalism + polished desktop UX + excellent laptop interaction + keyboard-first workflow + native FuhrerOS identity.**

FuhrerOS should feel like a modern operating system rather than a hobby OS with a graphical shell attached.

---

# 2. Design References

Use the following systems as references.

Do NOT copy their implementations or branding.

### Primary visual reference

Modern Arch Linux desktop configurations, especially:

* Hyprland
* Waybar
* modern dark Linux desktop configurations
* minimalist terminal-centric workflows

These provide inspiration for:

* minimalism
* tiling
* keyboard-driven workflows
* workspace-centric interaction
* compact status bars
* customization

### Secondary references

Study:

* GNOME
* KDE Plasma
* COSMIC
* Windows 11
* macOS
* ChromeOS
* Niri

Use each system for the interaction patterns it handles well.

The objective is NOT:

```text
Make FuhrerOS look like Arch.
```

The objective is:

```text
Build a FuhrerOS-native desktop influenced by
the best ideas from modern desktop environments.
```

---

# 3. Overall Visual Identity

The default FuhrerOS desktop should be:

* dark-first
* minimal
* modern
* lightweight
* technical
* elegant
* highly responsive
* keyboard-friendly
* touchpad-friendly

Avoid:

* excessive gradients
* excessive blur
* excessive transparency
* giant widgets
* unnecessary animations
* visual clutter
* skeuomorphic elements
* unnecessary desktop icons

The interface should feel lightweight even on modest hardware.

---

# 4. Suggested Visual Language

### Windows

Use:

* approximately 8–12 px corner radius
* subtle borders
* subtle shadows
* compact title bars
* consistent padding
* clear focus state

Example:

```text
┌──────────────────────────────────────────────┐
│ ● Terminal                              − □ ×│
├──────────────────────────────────────────────┤
│                                              │
│                                              │
│                  Terminal                    │
│                                              │
└──────────────────────────────────────────────┘
```

Avoid excessively large title bars.

---

# 5. Desktop Layout

The default desktop should contain:

```text
┌─────────────────────────────────────────────────────────┐
│ F │  1  2  3  4  5  │       CPU RAM NET       │ 22:41 │
├─────────────────────────────────────────────────────────┤
│                                                         │
│                                                         │
│                   APPLICATION WINDOWS                  │
│                                                         │
│                                                         │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

The top bar should be compact.

### Left

FuhrerOS logo / launcher.

### Center

Workspace indicators.

### Right

System indicators:

* CPU
* RAM
* network
* volume
* battery
* brightness
* clock

Clicking the system-status area should open the Control Center.

---

# 6. Workspace System

Use a workspace-centric desktop.

Example:

```text
1    2    3    4    5
●    ○    ○    ○    ○
```

Workspace switching should be extremely fast.

Users should be able to:

* create workspaces
* remove workspaces
* rename workspaces
* move windows between workspaces
* move windows while switching workspaces
* configure workspace behavior

Suggested default workspace names:

```text
1 — General
2 — Development
3 — Browser
4 — Research
5 — Misc
```

But users must be able to change them.

---

# 7. Window Management

Support both:

### Floating mode

Traditional desktop windows.

### Tiling mode

Modern dynamic tiling.

Example:

```text
┌──────────────────────┬────────────────────┐
│                      │                    │
│       Terminal       │       Editor       │
│                      │                    │
├──────────────────────┤                    │
│                      │                    │
│       Terminal       │                    │
│                      │                    │
└──────────────────────┴────────────────────┘
```

### Focus mode

One application fills the workspace.

The user should be able to switch between:

```text
Floating
Tiling
Focus
```

without restarting applications.

---

# 8. Tiling Philosophy

Take inspiration from Hyprland and Niri but don't blindly reproduce either model.

The user should be able to:

* split windows
* resize splits
* move windows
* swap windows
* change orientation
* create tabs
* temporarily float a window
* return a floating window to the tile tree

Keyboard interaction should be first-class.

---

# 9. Application Launcher

Pressing:

```text
Super
```

should open a launcher.

Example:

```text
┌─────────────────────────────────────────────┐
│                                             │
│ 🔍  Search applications, files or commands  │
│                                             │
├─────────────────────────────────────────────┤
│                                             │
│ Terminal        Browser        Files        │
│ Settings        Monitor        Calculator   │
│                                             │
└─────────────────────────────────────────────┘
```

The launcher should support:

### Applications

```text
terminal
browser
files
settings
monitor
```

### Commands

```text
wifi
volume
brightness
lock
shutdown
reboot
screenshot
```

### File search

```text
report.pdf
project/
notes/
```

### System navigation

```text
workspace 3
open Downloads
```

The launcher should be keyboard-first.

---

# 10. Fuhrer Command Palette

Eventually the launcher should evolve into a command palette.

Example:

```text
> workspace 3
> open ~/Downloads
> cpu
> network
> volume 50
> screenshot
> lock
```

This should be a native FuhrerOS capability rather than a third-party shell.

---

# 11. Control Center

Create a distinctive FuhrerOS Control Center.

Example:

```text
┌────────────────────────────────────────────────┐
│ FUHRER CONTROL CENTER                          │
├────────────────────────────────────────────────┤
│                                                │
│ CPU                         MEMORY             │
│ █████████░░ 73%            ██████░░░ 58%       │
│                                                │
│ NETWORK                     STORAGE            │
│ ↓ 4.2 MB/s                  ↑ 18 MB/s          │
│                                                │
├────────────────────────────────────────────────┤
│ WORKLOAD                                       │
│                                                │
│ Interactive / I/O Bound                        │
│                                                │
│ SCHEDULER                                      │
│ Adaptive Low Latency                           │
│                                                │
│ CACHE                                          │
│ Working Set                                    │
│                                                │
│ NETWORK                                        │
│ Latency Optimized                              │
│                                                │
├────────────────────────────────────────────────┤
│ Last policy transition: 183 ms ago             │
│ Policy transitions today: 17                   │
└────────────────────────────────────────────────┘
```

This should expose the adaptive-kernel research in a user-friendly way.

---

# 12. Adaptive OS Visualization

FuhrerOS should make its research contribution visible.

When the adaptive controller changes policy, optionally show a subtle notification:

```text
Adaptive policy

Workload changed:
CPU-bound → Interactive

Scheduler:
Throughput → Low Latency
```

This must NOT interrupt the user.

Use subtle notifications or a system-monitor history.

Never create intrusive popups for normal policy changes.

---

# 13. Keyboard Shortcuts

Default shortcuts:

```text
Super
    Launcher

Alt + Tab
    Application switcher

Super + Tab
    Window overview

Super + Left
    Focus/move window left

Super + Right
    Focus/move window right

Super + Up
    Maximize

Super + Down
    Restore/minimize

Super + D
    Show desktop

Super + L
    Lock screen

Ctrl + Alt + T
    Terminal

Super + 1..9
    Workspace

Super + Shift + 1..9
    Move window to workspace

Super + Shift + Q
    Close application
```

All shortcuts should eventually be configurable.

Avoid creating shortcuts that conflict unnecessarily with common Linux, Windows, or application shortcuts.

---

# 14. Modern Touchpad Support

Touchpad interaction is a **first-class FuhrerOS requirement**.

The target is a modern laptop with a Windows Precision Touchpad.

Do not implement only basic mouse emulation.

The input subsystem should eventually understand:

```text
pointer
tap
double tap
drag
scroll
pinch
swipe
multi-finger tap
```

---

# 15. One-Finger Interaction

Support:

### Movement

One-finger movement:

```text
finger → pointer
```

### Tap

One-finger tap:

```text
primary click
```

### Double tap

One-finger double tap:

```text
double click
```

### Drag

Tap-and-drag should support normal pointer dragging.

Provide an optional drag-lock setting.

---

# 16. Two-Finger Gestures

### Two-finger scrolling

Support:

* vertical scrolling
* horizontal scrolling
* smooth/inertial scrolling

The scrolling experience should feel continuous rather than producing discrete scroll jumps.

### Natural scrolling

Provide:

```text
Natural scrolling: ON/OFF
```

### Two-finger tap

Default:

```text
right click
```

### Two-finger pinch

Default:

```text
zoom
```

Applications may use zoom when appropriate.

The desktop should provide a normalized pinch event.

---

# 17. Advanced Two-Finger Behavior

Support configurable:

* scroll speed
* scroll acceleration
* natural/reverse scrolling
* tap-to-click
* touchpad sensitivity
* pointer acceleration
* click pressure where hardware supports it

Do not hard-code these settings.

They belong in Settings → Touchpad.

---

# 18. Three-Finger Gestures

Three-finger gestures should control desktop navigation.

### Swipe left

Previous workspace/application depending on configured mode.

### Swipe right

Next workspace/application.

### Swipe up

Open overview:

```text
┌─────────────────────────────────────────────┐
│ Workspace 1   Workspace 2   Workspace 3     │
│                                             │
│   ┌───────┐     ┌───────┐     ┌───────┐     │
│   │       │     │       │     │       │     │
│   │ Apps  │     │ Apps  │     │ Apps  │     │
│   │       │     │       │     │       │     │
│   └───────┘     └───────┘     └───────┘     │
└─────────────────────────────────────────────┘
```

### Swipe down

Show desktop/minimize current workspace windows depending on user configuration.

---

# 19. Four-Finger Gestures

Four-finger gestures should be reserved for workspace-level operations.

### Four-finger left/right

Switch workspace.

### Four-finger up

Workspace overview.

### Four-finger tap

Configurable.

Default:

```text
open Fuhrer Control Center
```

The user should be able to disable four-finger gestures.

---

# 20. Gesture Customization

Settings should eventually provide:

```text
Settings
  → Touchpad
```

with:

```text
Touchpad
────────────────────────

Enable Touchpad                 ON

Tap to Click                    ON

Natural Scrolling               ON

Pointer Speed
──────────●────────

Scroll Speed
───────●──────────

Three-finger swipe left
[ Previous Workspace       ▼ ]

Three-finger swipe right
[ Next Workspace            ▼ ]

Three-finger swipe up
[ Overview                  ▼ ]

Three-finger swipe down
[ Show Desktop              ▼ ]

Four-finger swipe left
[ Previous Workspace        ▼ ]

Four-finger swipe right
[ Next Workspace             ▼ ]

Four-finger tap
[ Control Center             ▼ ]
```

---

# 21. Palm Rejection

Palm rejection is important for laptop usability.

The input subsystem should distinguish likely:

```text
finger
palm
accidental contact
```

where hardware information permits.

The desktop should avoid interpreting accidental palm contact as pointer movement or gestures.

---

# 22. Disable Touchpad While Typing

Provide:

```text
Disable touchpad while typing: ON
```

with configurable delay.

Example:

```text
Disable for:
100 ms
250 ms
500 ms
1 s
```

This is particularly important for terminal and coding workloads.

---

# 23. Touchpad Accessibility

Provide:

* slower pointer speed
* larger gesture recognition window
* configurable gesture sensitivity
* disable multi-finger gestures
* disable tap-to-click
* reduced motion
* alternative navigation methods
* keyboard-only desktop navigation

---

# 24. Gesture Recognition Architecture

The kernel/input architecture should eventually resemble:

```text
Physical Touchpad
       ↓
Input Driver
       ↓
Raw Touch Events
       ↓
Fuhrer Input Layer
       ↓
Gesture Recognizer
       ↓
Normalized Gesture Events
       ↓
Desktop Shell
       ↓
Window Manager / Workspace Manager
```

Example normalized events:

```text
POINTER_MOVE
POINTER_BUTTON_DOWN
POINTER_BUTTON_UP

TAP
DOUBLE_TAP
TWO_FINGER_TAP

SCROLL
PINCH

THREE_SWIPE_LEFT
THREE_SWIPE_RIGHT
THREE_SWIPE_UP
THREE_SWIPE_DOWN

FOUR_SWIPE_LEFT
FOUR_SWIPE_RIGHT
FOUR_SWIPE_UP
FOUR_SWIPE_DOWN
FOUR_TAP
```

Applications should not directly process raw touchpad hardware events unless explicitly required.

---

# 25. Gesture Conflict Resolution

System-level gestures must have priority over application gestures where appropriate.

For example:

```text
Three-finger workspace swipe
```

should not accidentally become:

```text
application gesture
```

unless the user has explicitly configured that behavior.

The gesture recognizer should have:

```text
system gesture
application gesture
```

priority rules.

---

# 26. Gesture Thresholds

Do not hard-code arbitrary thresholds without measurement.

Parameters should include:

```text
minimum swipe distance
maximum swipe duration
minimum finger separation
pinch threshold
velocity threshold
gesture timeout
```

These should eventually be configurable or calibrated.

Record gesture-tuning decisions in:

```text
research-log/
```

---

# 27. Animation

Animations should be subtle and fast.

Suggested targets:

```text
window open:
100–160 ms

window close:
80–140 ms

workspace transition:
150–220 ms

launcher:
100–150 ms

overview:
150–250 ms
```

These are initial targets, not fixed requirements.

Measure perceived responsiveness.

Animations must be:

* interruptible
* non-blocking
* frame-rate independent
* disabled/reduced when reduced-motion is enabled

---

# 28. Reduced Motion

Settings should provide:

```text
Accessibility
    → Reduce Motion
```

When enabled:

* disable workspace slide animations
* minimize window animations
* remove unnecessary scaling effects
* avoid parallax
* keep transitions immediate

---

# 29. Themes

Support at least:

```text
Dark
Light
```

Eventually support:

```text
Auto
```

which follows system time or user preference.

Accent colors should be configurable.

Suggested default aesthetic:

```text
dark neutral background
light text
single accent color
subtle secondary accent
```

Do not hard-code one accent color permanently.

---

# 30. Typography

Prioritize:

* readability
* high DPI support
* consistent font metrics
* clear hierarchy

Use a modern sans-serif UI font.

The terminal may use a monospaced font independently.

Do not make the UI depend on a single proprietary font.

---

# 31. Icons

Use a consistent icon family.

Prefer:

* simple
* geometric
* recognizable
* scalable

Avoid mixing multiple icon styles.

Icons should remain readable at small sizes.

---

# 32. Notifications

Notifications should be:

* compact
* grouped
* dismissible
* non-blocking

Example:

```text
┌───────────────────────────────────┐
│ FuhrerOS                           │
│ Build completed successfully      │
│ 2m ago                             │
└───────────────────────────────────┘
```

System-policy changes should normally appear only in the Control Center/history, not as disruptive notifications.

---

# 33. File Manager

The file manager should use a clean two-panel or sidebar-oriented layout.

Example:

```text
┌──────────────────────────────────────────────────┐
│ ← →   /home/user/Documents              🔍       │
├───────────────┬──────────────────────────────────┤
│ Home          │                                  │
│ Desktop       │  📁 Projects                     │
│ Documents     │  📁 Downloads                    │
│ Downloads     │  📄 report.pdf                   │
│ Pictures      │  📄 notes.txt                    │
│               │                                  │
└───────────────┴──────────────────────────────────┘
```

It should expose the FuhrerOS VFS naturally.

---

# 34. Terminal

The terminal should be a first-class application.

Features:

* tabs
* split panes eventually
* search
* copy/paste
* keyboard shortcuts
* scrollback
* configurable font size
* configurable opacity if compositor supports it

It should launch quickly.

---

# 35. System Monitor

Create a native system monitor.

Display:

```text
CPU
Memory
Processes
Threads
Disk
Network
Scheduler
Cache
```

Allow process inspection.

Eventually show:

```text
PID
Process
CPU
Memory
State
Priority
Scheduler Policy
```

---

# 36. Research Dashboard

The system monitor should have an optional research view.

Example:

```text
FUHRER RESEARCH

Current workload:
Interactive / I/O Bound

Scheduler:
Adaptive

Scheduler quantum:
4 ms

Cache:
Working Set

Network:
Latency Optimized

Policy transitions:
17

Profiler overhead:
0.7%

Last transition:
183 ms ago
```

This makes the BTP research visible without exposing unnecessary kernel internals to normal users.

---

# 37. Browser

The browser is a long-term compatibility target.

Do NOT write a browser engine from scratch.

The browser should eventually be used as a demanding real-world workload.

It will test:

* process management
* memory management
* graphics
* networking
* filesystem
* IPC
* input
* scheduling

---

# 38. HiDPI

The UI must not assume:

```text
1920 × 1080
```

Support logical display coordinates.

Target:

```text
1920×1080
2560×1440
2880×1800
3840×2160
```

and scaling:

```text
100%
125%
150%
175%
200%
```

The compositor should render using logical coordinates and scale to physical pixels.

---

# 39. VM Compatibility

The desktop must run in the initial QEMU environment.

Initial target:

```text
2 vCPU
2 GB RAM
x86-64
QEMU
```

Benchmark target:

```text
4 vCPU
4 GB RAM
```

The UI must remain usable within these constraints.

Do not design an interface that requires a modern discrete GPU.

---

# 40. Performance Philosophy

Visual quality must NOT come at the cost of FuhrerOS's research goals.

Avoid:

* unnecessary GPU effects
* huge textures
* excessive blur
* constantly running animations
* polling every millisecond
* unnecessary background processes

Measure:

```text
compositor CPU
compositor memory
frame time
input latency
gesture latency
workspace latency
launcher latency
window creation latency
```

---

# 41. UI Research Opportunity

The desktop itself can become part of the OS research.

Possible experiment:

```text
Workload:
Browser + Terminal + Compilation

Compare:

Static scheduler
vs
Adaptive scheduler
```

Measure:

```text
terminal response latency
workspace switching latency
browser responsiveness
compilation completion time
CPU utilization
```

This demonstrates whether the adaptive kernel can preserve interactive responsiveness while executing background workloads.

---

# 42. Native Architecture

The intended architecture is:

```text
                    FUHREROS DESKTOP

                         Desktop Shell
                              │
                     Window Manager
                              │
                        Compositor
                              │
             ┌────────────────┼────────────────┐
             │                │                │
         Input Layer      Renderer        Workspace
             │                │            Manager
             │                │
        Touchpad          Framebuffer
        Keyboard             │
        Mouse                │
             └───────────────┴────────────────┘
                             │
                        FuhrerOS Kernel
                             │
                  ┌──────────┼──────────┐
                  │          │          │
               Memory    Scheduler     IPC
                  │          │          │
                  └──────────┼──────────┘
                             │
                          Hardware
```

Do not use Linux, GNOME, KDE, Hyprland, or another operating system as the underlying desktop implementation.

These are references only.

---

# 43. Implementation Order

Do not implement all UI features simultaneously.

Recommended order:

```text
1. Framebuffer
2. Font rendering
3. Cursor
4. Basic input
5. Window surfaces
6. Compositor
7. Floating windows
8. Tiling
9. Workspace system
10. Keyboard shortcuts
11. Launcher
12. Desktop shell
13. Touchpad abstraction
14. Basic gestures
15. Settings
16. Control Center
17. Advanced touchpad gestures
18. File manager
19. System monitor
20. Browser compatibility
21. UI performance optimization
```

---

# 44. Design Decision Logging

For every significant UI decision create:

```text
UI-D-XXX
```

with:

```text
Problem:
Reference systems:
Options:
Chosen design:
Reason:
Performance impact:
Accessibility impact:
Implementation:
Validation:
```

Example:

```text
UI-D-001

Problem:
How should workspace navigation work?

References:
GNOME
Hyprland
Windows
Niri

Options:
Vertical workspaces
Horizontal workspaces
Scrolling workspaces

Decision:
Horizontal workspaces

Reason:
Works naturally with three-finger horizontal gestures
and is familiar to users of GNOME and modern Linux desktops.

Validation:
Measure workspace-switch latency and user interaction.
```

---

# 45. Final UX Goal

The final FuhrerOS experience should feel approximately like:

```text
                    FUHREROS

       ┌─────────────────────────────────────┐
       │ F │ 1  2  3  4 │ CPU RAM NET │ 22:41│
       └─────────────────────────────────────┘

       ┌───────────────────┬─────────────────┐
       │                   │                 │
       │     TERMINAL      │     BROWSER     │
       │                   │                 │
       ├───────────────────┤                 │
       │                   │                 │
       │      EDITOR       │                 │
       │                   │                 │
       └───────────────────┴─────────────────┘

             Super → Launcher

       3-finger ← → Workspace

       3-finger ↑ → Overview

       Super + F → Fuhrer Control Center
```

The goal is:

> **A lightweight, beautiful, keyboard-first and touchpad-first desktop that feels familiar to a Linux/Windows laptop user while exposing the unique capabilities of the FuhrerOS kernel.**

The UI should not merely demonstrate that FuhrerOS works.

It should make the user want to use it.
