# Input

```text
PS/2 keyboard ─┐                          ┌─► console TTY (no desktop)
PS/2 mouse ────┼─► input events ─► sink ──┤
touchpad* ─► contacts ─► gesture          └─► compositor (desktop)
               recognizer ─┘
```

- **i8042 driver** (`drivers/input/ps2.c`): keyboard (scan-code set 1 via
  controller translation; modifiers, extended keys, Super, arrows, F-keys,
  Ctrl-letter control codes) and mouse (IntelliMouse wheel detection).
  Both IRQs drain the shared controller buffer and dispatch each byte by the
  AUX status bit (an early bug fed mouse bytes to the keyboard, F-110).
- **Events** (`include/input.h`): key, pointer motion, button, scroll,
  gesture, touch.
- **Gesture recognizer** (`drivers/input/input.c`, §27):
  1 finger → pointer, tap = left click; 2 fingers → scroll, pinch, tap =
  right click; 3 fingers → swipe up (overview), down (desktop), left/right
  (app switch); 4 fingers → swipe left/right (workspace).
  QEMU exposes no multi-touch device, so it is verified with synthetic
  contact frames in the late self-tests (`gesture.*`, D-117); a real
  touchpad driver (I²C-HID / Precision Touchpad) is future work.
- **Console TTY** (`drivers/tty.c`): canonical and raw modes, echo, line
  editing, Ctrl-C (kills the foreground process), Ctrl-D, arrow keys as ANSI
  sequences; serial input is merged, so headless sessions work.
