/* Framebuffer drawing primitives. Everything is clipped to the surface. */
#include "gfx/fb.h"
#include "kernel.h"

static struct surface screen;
static bool have_fb;

void fb_init(void)
{
	if (!boot.fb_virt || boot.fb_bpp != 32)
		return;
	screen.px = boot.fb_virt;
	screen.w = boot.fb_width;
	screen.h = boot.fb_height;
	screen.stride = boot.fb_pitch / 4;
	have_fb = true;
}

bool fb_present(void) { return have_fb; }
struct surface *fb_screen(void) { return have_fb ? &screen : NULL; }

static bool clip(struct surface *s, int *x, int *y, int *w, int *h)
{
	if (*x < 0) { *w += *x; *x = 0; }
	if (*y < 0) { *h += *y; *y = 0; }
	if (*x + *w > (int)s->w) *w = (int)s->w - *x;
	if (*y + *h > (int)s->h) *h = (int)s->h - *y;
	return *w > 0 && *h > 0;
}

void surf_fill(struct surface *s, int x, int y, int w, int h, u32 c)
{
	if (!clip(s, &x, &y, &w, &h))
		return;
	for (int j = 0; j < h; j++) {
		u32 *row = s->px + (size_t)(y + j) * s->stride + x;
		for (int i = 0; i < w; i++)
			row[i] = c;
	}
}

void surf_rect(struct surface *s, int x, int y, int w, int h, u32 c)
{
	surf_fill(s, x, y, w, 1, c);
	surf_fill(s, x, y + h - 1, w, 1, c);
	surf_fill(s, x, y, 1, h, c);
	surf_fill(s, x + w - 1, y, 1, h, c);
}

static inline u32 blend(u32 dst, u32 src, u32 a)
{
	u32 rb = ((src & 0xFF00FF) * a + (dst & 0xFF00FF) * (255 - a)) >> 8;
	u32 g = ((src & 0x00FF00) * a + (dst & 0x00FF00) * (255 - a)) >> 8;
	return (rb & 0xFF00FF) | (g & 0x00FF00);
}

void surf_blend_fill(struct surface *s, int x, int y, int w, int h, u32 c, u8 alpha)
{
	if (!clip(s, &x, &y, &w, &h))
		return;
	for (int j = 0; j < h; j++) {
		u32 *row = s->px + (size_t)(y + j) * s->stride + x;
		for (int i = 0; i < w; i++)
			row[i] = blend(row[i], c, alpha);
	}
}

static const struct logo_image *logo_pick(int height)
{
	const struct logo_image *best = &logo_images[0];
	for (int i = 0; i < logo_image_count; i++)
		if (logo_images[i].h <= height)
			best = &logo_images[i];
	return best;
}

int logo_width(int height) { return logo_pick(height)->w; }

int surf_logo(struct surface *s, int x, int y, int height, u32 fg, u32 accent)
{
	const struct logo_image *l = logo_pick(height);
	for (int j = 0; j < l->h; j++) {
		int py = y + j;
		if (py < 0 || py >= (int)s->h)
			continue;
		u32 *row = s->px + (size_t)py * s->stride;
		for (int i = 0; i < l->w; i++) {
			int px = x + i;
			if (px < 0 || px >= (int)s->w)
				continue;
			u8 a = l->fg[j * l->w + i], b = l->accent[j * l->w + i];
			if (a)
				row[px] = blend(row[px], fg, a);
			if (b)
				row[px] = blend(row[px], accent, b);
		}
	}
	return l->w;
}

void surf_round_rect(struct surface *s, int x, int y, int w, int h, int r, u32 c)
{
	for (int j = 0; j < h; j++) {
		int inset = 0;
		int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
		if (dy) {
			/* integer circle: inset = r - sqrt(r^2 - dy^2) */
			int t = r * r - dy * dy, q = 0;
			while ((q + 1) * (q + 1) <= t)
				q++;
			inset = r - q;
		}
		surf_fill(s, x + inset, y + j, w - 2 * inset, 1, c);
	}
}

void surf_char(struct surface *s, int x, int y, u8 ch, u32 fg, u32 bg, bool transparent)
{
	const u8 *g = font8x16[ch];
	for (int j = 0; j < FONT_H; j++) {
		int py = y + j;
		if (py < 0 || py >= (int)s->h)
			continue;
		u32 *row = s->px + (size_t)py * s->stride;
		u8 bits = g[j];
		for (int i = 0; i < FONT_W; i++) {
			int px = x + i;
			if (px < 0 || px >= (int)s->w)
				continue;
			if (bits & (0x80 >> i))
				row[px] = fg;
			else if (!transparent)
				row[px] = bg;
		}
	}
}

int surf_text(struct surface *s, int x, int y, const char *t, u32 fg)
{
	int x0 = x;
	for (; *t; t++) {
		if (*t == '\n') {
			y += FONT_H;
			x = x0;
			continue;
		}
		surf_char(s, x, y, (u8)*t, fg, 0, true);
		x += FONT_W;
	}
	return x - x0;
}

void surf_text_scaled(struct surface *s, int x, int y, const char *t, u32 fg, int scale)
{
	for (; *t; t++, x += FONT_W * scale) {
		const u8 *g = font8x16[(u8)*t];
		for (int j = 0; j < FONT_H; j++)
			for (int i = 0; i < FONT_W; i++)
				if (g[j] & (0x80 >> i))
					surf_fill(s, x + i * scale, y + j * scale, scale, scale, fg);
	}
}

void surf_blit(struct surface *dst, int dx, int dy, const struct surface *src, int sx, int sy,
	       int w, int h)
{
	if (sx < 0) { w += sx; dx -= sx; sx = 0; }
	if (sy < 0) { h += sy; dy -= sy; sy = 0; }
	if (sx + w > (int)src->w) w = (int)src->w - sx;
	if (sy + h > (int)src->h) h = (int)src->h - sy;
	int ox = dx, oy = dy;
	if (!clip(dst, &dx, &dy, &w, &h))
		return;
	sx += dx - ox;
	sy += dy - oy;
	for (int j = 0; j < h; j++)
		memcpy(dst->px + (size_t)(dy + j) * dst->stride + dx,
		       src->px + (size_t)(sy + j) * src->stride + sx, (size_t)w * 4);
}
