/* Framebuffer primitives (32 bpp linear framebuffer, xRGB). */
#ifndef GFX_FB_H
#define GFX_FB_H

#include "types.h"

#define RGB(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

struct surface {
	u32 *px;
	u32 w, h;
	u32 stride;	/* in pixels */
};

extern const u8 font8x16[256][16];
#define FONT_W 8
#define FONT_H 16

void fb_init(void);
bool fb_present(void);
struct surface *fb_screen(void);	/* the real framebuffer */

void surf_fill(struct surface *s, int x, int y, int w, int h, u32 c);
void surf_rect(struct surface *s, int x, int y, int w, int h, u32 c);
void surf_blend_fill(struct surface *s, int x, int y, int w, int h, u32 c, u8 alpha);
void surf_char(struct surface *s, int x, int y, u8 ch, u32 fg, u32 bg, bool transparent);
int surf_text(struct surface *s, int x, int y, const char *t, u32 fg);
void surf_text_scaled(struct surface *s, int x, int y, const char *t, u32 fg, int scale);
void surf_blit(struct surface *dst, int dx, int dy, const struct surface *src, int sx, int sy,
	       int w, int h);
void surf_round_rect(struct surface *s, int x, int y, int w, int h, int r, u32 c);

/* Early text console on the framebuffer. */
void fbcon_init(void);
void fbcon_write(const char *s, size_t n);
void fbcon_clear(void);
void fbcon_set_colors(u32 fg, u32 bg);
bool fbcon_active(void);
void fbcon_detach(void);	/* the compositor takes over the screen */

#endif
