# Graphics

Pipeline (§24): **framebuffer → Fuhrer compositor → window manager → desktop
shell**. No X11/Wayland; nothing from Linux.

- **Framebuffer** (`gfx/fb.c`): the linear 32-bpp framebuffer from Limine,
  mapped write-combining. Primitives: fill, rect, alpha blend, rounded rect,
  8×16 text (Spleen font, BSD-2), scaled text, blit.
- **Boot console** (`gfx/fbcon.c`): character-cell grid in RAM; rendering
  only writes video memory, once per write call (F-104). ANSI colours.
- **Compositor** (`gfx/compositor.c`, kernel thread `compositor`):
  back buffer in RAM, damage rectangle, ≤ 60 fps cap, redraw only when
  something changed (plus a once-per-second panel refresh); the damaged
  region is copied to the framebuffer. Composition holds a mutex, not
  interrupts-off, so it does not distort scheduling measurements.
- **Surfaces:** each window gets a full-screen-sized pixel buffer (so it can
  grow without remapping) mapped into the owning process at
  `0x3000_0000_0000 + id × 64 MiB`. Apps draw directly and call
  `win_present`.
- **Cursor, shadows, decorations, panel, launcher, overview thumbnails
  (nearest-neighbour scaling), lock screen** are drawn by the compositor.

Statistics: `/proc/desktop` (windows, frames, average composition time).
Acceleration (virtio-gpu) is future work.
