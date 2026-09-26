/* libfu GUI toolkit: windows backed by compositor surfaces, drawing
 * primitives, text (the kernel's Spleen 8x16 font) and small widgets. */
#ifndef FU_GUI_H
#define FU_GUI_H
#include "fu.h"

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define FONT_W 8
#define FONT_H 16

/* key codes (same values as kernel/include/input.h) */
#define K_BACKSPACE 8
#define K_TAB 9
#define K_ENTER 10
#define K_ESC 27
#define K_UP 0x100
#define K_DOWN 0x101
#define K_LEFT 0x102
#define K_RIGHT 0x103
#define K_HOME 0x104
#define K_END 0x105
#define K_PGUP 0x106
#define K_PGDN 0x107
#define K_DELETE 0x109
#define M_SHIFT 1
#define M_CTRL 2

/* shared theme: the desktop's current colours (dark/light + accent),
 * fetched by gwin_open and refreshed when a WEV_THEME event arrives */
extern struct fu_theme fu_theme;
#define T_BG fu_theme.bg
#define T_BG2 fu_theme.bg2
#define T_FG fu_theme.fg
#define T_DIM fu_theme.dim
#define T_ACCENT fu_theme.accent
#define T_GOOD fu_theme.good
#define T_WARN fu_theme.warn
#define T_BAD fu_theme.bad
#define T_BORDER fu_theme.border
void theme_refresh(void);

struct gwin {
	int id;
	uint32_t *px;
	int w, h;
	int stride;
};

struct button {
	int x, y, w, h;
	const char *label;
	bool active;
};

int gwin_open(struct gwin *g, int w, int h, const char *title);
void gwin_present(struct gwin *g);
void gwin_close(struct gwin *g);
/* Wait for an event on this window (timeout 0 = forever). */
int gwin_event(struct gwin *g, struct fu_wevent *ev, uint64_t timeout_ms);
void gwin_title(struct gwin *g, const char *t);

void g_fill(struct gwin *g, int x, int y, int w, int h, uint32_t c);
void g_rect(struct gwin *g, int x, int y, int w, int h, uint32_t c);
void g_round(struct gwin *g, int x, int y, int w, int h, int r, uint32_t c);
void g_char(struct gwin *g, int x, int y, unsigned char ch, uint32_t fg, uint32_t bg, bool transparent);
int g_text(struct gwin *g, int x, int y, const char *s, uint32_t fg);
void g_textn(struct gwin *g, int x, int y, const char *s, int n, uint32_t fg);
void g_text_big(struct gwin *g, int x, int y, const char *s, uint32_t fg, int scale);
uint32_t g_mix(uint32_t a, uint32_t b, int t); /* t/255 of b over a */
void g_bar(struct gwin *g, int x, int y, int w, int h, int pct, uint32_t c);
void g_button(struct gwin *g, const struct button *b);
bool button_hit(const struct button *b, int x, int y);
/* Simple line graph of `n` samples in [0,100]. */
void g_graph(struct gwin *g, int x, int y, int w, int h, const int *samples, int n, uint32_t c);

#endif
