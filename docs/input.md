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
  - 1 finger: pointer; a tap is a left click.
  - 2 fingers: scroll or pinch; a tap is a right click.
  - 3 and 4 fingers: swipes up, down, left and right, and taps.

  The recognizer only reports *which* gesture happened. The desktop maps
  each 3/4-finger gesture to an action (workspace, overview, desktop,
  window, launcher, Control Center) and gives system gestures priority over
  applications (UI-D-006). A pinch reaches the focused app as a normalised
  zoom event (Ctrl+scroll).
- **Touchpad options** (`input_set_options`, Settings → Touchpad):
  - tap-to-click;
  - natural scrolling (content follows the fingers);
  - pointer and scroll speed (1–8);
  - disable-while-typing: touch sequences that start within N ms of a key
    press are ignored.

  Palm rejection by contact size is not possible, because touch frames
  carry no contact size.
- **Verification:** QEMU exposes no multi-touch device, so the recognizer
  and the options are verified with synthetic contact frames in the late
  self-tests. The tests are:
  - `gesture.three_swipe_up`, `four_swipe_right`, `four_swipe_up`;
  - `two_finger_scroll`, `pinch`, `two_finger_tap_right_click`;
  - `three_and_four_finger_tap`, `one_finger_pointer`;
  - `natural_scroll`, `tap_off_and_typing_block`.

  See D-117. A real touchpad driver (I²C-HID / Precision Touchpad) is future
  work, and the thresholds are UNKNOWN - REQUIRES VERIFICATION on hardware.
- **Console TTY** (`drivers/tty.c`): canonical and raw modes, echo, line
  editing, Ctrl-C (kills the foreground process), Ctrl-D, arrow keys as ANSI
  sequences; serial input is merged, so headless sessions work.
