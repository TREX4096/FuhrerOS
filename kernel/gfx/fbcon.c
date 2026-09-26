/* Framebuffer text console with a tiny ANSI subset (SGR colours, clear,
 * cursor home). Used from boot until the compositor owns the screen. */
#include "gfx/fb.h"
#include "kernel.h"

#define MARGIN 8
static const u32 ansi_colors[16] = {
	RGB(0x1d, 0x23, 0x30), RGB(0xe0, 0x6c, 0x75), RGB(0x98, 0xc3, 0x79), RGB(0xe5, 0xc0, 0x7b),
	RGB(0x61, 0xaf, 0xef), RGB(0xc6, 0x78, 0xdd), RGB(0x56, 0xb6, 0xc2), RGB(0xdc, 0xdf, 0xe4),
	RGB(0x5c, 0x63, 0x70), RGB(0xff, 0x7b, 0x86), RGB(0xb5, 0xe8, 0x90), RGB(0xff, 0xd8, 0x8a),
	RGB(0x82, 0xc8, 0xff), RGB(0xe3, 0x9a, 0xf5), RGB(0x7f, 0xdb, 0xe6), RGB(0xff, 0xff, 0xff),
};
#define DEFAULT_FG ansi_colors[7]
#define DEFAULT_BG RGB(0x14, 0x18, 0x22)

static struct surface *scr;
static int cols, rows, cx, cy;
static u32 fg, bg;
static bool active;
static int esc_state;		/* 0 none, 1 got ESC, 2 in CSI */
static int esc_args[4], esc_n;

bool fbcon_active(void) { return active; }
void fbcon_detach(void) { active = false; }

void fbcon_set_colors(u32 f, u32 b)
{
	fg = f;
	bg = b;
}

void fbcon_clear(void)
{
	if (!scr)
		return;
	surf_fill(scr, 0, 0, (int)scr->w, (int)scr->h, bg);
	cx = cy = 0;
}

void fbcon_init(void)
{
	scr = fb_screen();
	if (!scr)
		return;
	cols = ((int)scr->w - 2 * MARGIN) / FONT_W;
	rows = ((int)scr->h - 2 * MARGIN) / FONT_H;
	fg = DEFAULT_FG;
	bg = DEFAULT_BG;
	fbcon_clear();
	active = true;
}

static void scroll(void)
{
	int y0 = MARGIN, h = rows * FONT_H;
	for (int y = 0; y < h - FONT_H; y++)
		memmove(scr->px + (size_t)(y0 + y) * scr->stride + MARGIN,
			scr->px + (size_t)(y0 + y + FONT_H) * scr->stride + MARGIN,
			(size_t)cols * FONT_W * 4);
	surf_fill(scr, MARGIN, y0 + h - FONT_H, cols * FONT_W, FONT_H, bg);
	cy = rows - 1;
}

static void newline(void)
{
	cx = 0;
	if (++cy >= rows)
		scroll();
}

static void sgr(void)
{
	for (int i = 0; i < (esc_n ? esc_n : 1); i++) {
		int a = esc_n ? esc_args[i] : 0;
		if (a == 0) { fg = DEFAULT_FG; bg = DEFAULT_BG; }
		else if (a == 1) { /* bold: brighten current colour */
			for (int k = 0; k < 8; k++)
				if (fg == ansi_colors[k]) fg = ansi_colors[k + 8];
		}
		else if (a >= 30 && a <= 37) fg = ansi_colors[a - 30];
		else if (a >= 90 && a <= 97) fg = ansi_colors[a - 90 + 8];
		else if (a >= 40 && a <= 47) bg = ansi_colors[a - 40];
		else if (a == 39) fg = DEFAULT_FG;
		else if (a == 49) bg = DEFAULT_BG;
	}
}

static void putch(char c)
{
	if (esc_state == 1) {
		if (c == '[') {
			esc_state = 2;
			esc_n = 0;
			esc_args[0] = 0;
		} else {
			esc_state = 0;
		}
		return;
	}
	if (esc_state == 2) {
		if (c >= '0' && c <= '9') {
			if (esc_n == 0) esc_n = 1;
			esc_args[esc_n - 1] = esc_args[esc_n - 1] * 10 + (c - '0');
			return;
		}
		if (c == ';') {
			if (esc_n < 4) esc_args[esc_n++] = 0;
			return;
		}
		esc_state = 0;
		if (c == 'm') sgr();
		else if (c == 'J') fbcon_clear();
		else if (c == 'H') cx = cy = 0;
		else if (c == 'K') surf_fill(scr, MARGIN + cx * FONT_W, MARGIN + cy * FONT_H,
					     (cols - cx) * FONT_W, FONT_H, bg);
		return;
	}
	switch (c) {
	case 0x1b: esc_state = 1; return;
	case '\n': newline(); return;
	case '\r': cx = 0; return;
	case '\t': cx = (cx + 8) & ~7; if (cx >= cols) newline(); return;
	case '\b':
		if (cx > 0) {
			cx--;
			surf_fill(scr, MARGIN + cx * FONT_W, MARGIN + cy * FONT_H, FONT_W, FONT_H, bg);
		}
		return;
	}
	surf_char(scr, MARGIN + cx * FONT_W, MARGIN + cy * FONT_H, (u8)c, fg, bg, false);
	if (++cx >= cols)
		newline();
}

void fbcon_write(const char *s, size_t n)
{
	if (!active)
		return;
	for (size_t i = 0; i < n; i++)
		putch(s[i]);
}

int fbcon_cols(void) { return cols ? cols : 80; }
int fbcon_rows(void) { return rows ? rows : 25; }
