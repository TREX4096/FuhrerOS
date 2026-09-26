/* libfu GUI toolkit implementation. */
#include "gui.h"

extern const uint8_t font8x16[256][16];

/* defaults (dark) until the desktop answers */
struct fu_theme fu_theme = { 1, RGB(0x16, 0x19, 0x21), RGB(0x22, 0x27, 0x32), RGB(0xe7, 0xe9, 0xee),
			     RGB(0x8b, 0x93, 0xa5), RGB(0xe8, 0x2a, 0x36), RGB(0x6f, 0xcf, 0x8a),
			     RGB(0xf2, 0xc1, 0x5b), RGB(0xe5, 0x48, 0x4d), RGB(0x2b, 0x30, 0x3b) };

void theme_refresh(void) { sys2(SYS_DESKTOP_CTL, DESK_THEME, &fu_theme); }

int gwin_open(struct gwin *g, int w, int h, const char *title)
{
	struct fu_screen s;
	if (sys1(SYS_WIN_INFO, &s) < 0 || !s.desktop_running)
		return -38;
	theme_refresh();
	int id = (int)sys3(SYS_WIN_CREATE, w, h, title);
	if (id < 0)
		return id;
	g->id = id;
	g->px = (uint32_t *)sys1(SYS_WIN_SURFACE, id);
	g->w = w;
	g->h = h;
	g->stride = (int)s.width; /* surfaces are allocated at full-screen width */
	return 0;
}

void gwin_present(struct gwin *g) { sys1(SYS_WIN_PRESENT, g->id); }
void gwin_close(struct gwin *g) { sys1(SYS_WIN_CLOSE, g->id); }
void gwin_title(struct gwin *g, const char *t) { sys2(SYS_WIN_SET_TITLE, g->id, t); }

int gwin_event(struct gwin *g, struct fu_wevent *ev, uint64_t timeout_ms)
{
	int r = (int)sys3(SYS_WIN_EVENT, g->id, ev, timeout_ms);
	if (r == 0 && ev->type == WEV_THEME)
		theme_refresh();
	if (r == 0 && ev->type == WEV_RESIZE) {
		g->w = ev->x;
		g->h = ev->y;
	}
	return r;
}

void g_fill(struct gwin *g, int x, int y, int w, int h, uint32_t c)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > g->w) w = g->w - x;
	if (y + h > g->h) h = g->h - y;
	for (int j = 0; j < h; j++) {
		uint32_t *row = g->px + (size_t)(y + j) * g->stride + x;
		for (int i = 0; i < w; i++)
			row[i] = c;
	}
}

void g_rect(struct gwin *g, int x, int y, int w, int h, uint32_t c)
{
	g_fill(g, x, y, w, 1, c);
	g_fill(g, x, y + h - 1, w, 1, c);
	g_fill(g, x, y, 1, h, c);
	g_fill(g, x + w - 1, y, 1, h, c);
}

void g_round(struct gwin *g, int x, int y, int w, int h, int r, uint32_t c)
{
	for (int j = 0; j < h; j++) {
		int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0), inset = 0;
		if (dy) {
			int t = r * r - dy * dy, q = 0;
			while ((q + 1) * (q + 1) <= t)
				q++;
			inset = r - q;
		}
		g_fill(g, x + inset, y + j, w - 2 * inset, 1, c);
	}
}

void g_char(struct gwin *g, int x, int y, unsigned char ch, uint32_t fg, uint32_t bg, bool tr)
{
	const uint8_t *gl = font8x16[ch];
	for (int j = 0; j < FONT_H; j++) {
		int py = y + j;
		if (py < 0 || py >= g->h)
			continue;
		uint32_t *row = g->px + (size_t)py * g->stride;
		for (int i = 0; i < FONT_W; i++) {
			int px = x + i;
			if (px < 0 || px >= g->w)
				continue;
			if (gl[j] & (0x80 >> i))
				row[px] = fg;
			else if (!tr)
				row[px] = bg;
		}
	}
}

void g_textn(struct gwin *g, int x, int y, const char *s, int n, uint32_t fg)
{
	for (int i = 0; i < n && s[i]; i++)
		g_char(g, x + i * FONT_W, y, (unsigned char)s[i], fg, 0, true);
}

int g_text(struct gwin *g, int x, int y, const char *s, uint32_t fg)
{
	int n = (int)strlen(s);
	g_textn(g, x, y, s, n, fg);
	return n * FONT_W;
}

void g_text_big(struct gwin *g, int x, int y, const char *s, uint32_t fg, int sc)
{
	for (; *s; s++, x += FONT_W * sc) {
		const uint8_t *gl = font8x16[(unsigned char)*s];
		for (int j = 0; j < FONT_H; j++)
			for (int i = 0; i < FONT_W; i++)
				if (gl[j] & (0x80 >> i))
					g_fill(g, x + i * sc, y + j * sc, sc, sc, fg);
	}
}

void g_bar(struct gwin *g, int x, int y, int w, int h, int pct, uint32_t c)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	g_round(g, x, y, w, h, h / 2, T_BORDER); /* track visible on cards too */
	int fw = w * pct / 100;
	if (fw > h)
		g_round(g, x, y, fw, h, h / 2, c);
	else if (fw > 0)
		g_fill(g, x + h / 4, y + h / 4, fw, h / 2, c);
}

void g_button(struct gwin *g, const struct button *b)
{
	g_round(g, b->x, b->y, b->w, b->h, 6, b->active ? T_ACCENT : g_mix(T_BG2, T_FG, 30));
	int tw = (int)strlen(b->label) * FONT_W;
	g_text(g, b->x + (b->w - tw) / 2, b->y + (b->h - FONT_H) / 2, b->label, b->active ? RGB(0xff, 0xff, 0xff) : T_FG);
}

bool button_hit(const struct button *b, int x, int y)
{
	return x >= b->x && y >= b->y && x < b->x + b->w && y < b->y + b->h;
}

void g_graph(struct gwin *g, int x, int y, int w, int h, const int *s, int n, uint32_t c)
{
	g_fill(g, x, y, w, h, T_BG2);
	for (int k = 1; k < 4; k++)
		g_fill(g, x, y + h * k / 4, w, 1, T_BORDER);
	if (n < 2)
		return;
	for (int i = 0; i < n; i++) {
		int px = x + i * (w - 1) / (n - 1);
		int v = s[i] < 0 ? 0 : s[i] > 100 ? 100 : s[i];
		int py = y + h - 1 - v * (h - 1) / 100;
		g_fill(g, px, py, 2, y + h - py, (c & 0xFEFEFE) >> 1); /* area */
		g_fill(g, px, py, 2, 2, c);
	}
}

uint32_t g_mix(uint32_t a, uint32_t b, int t)
{
	uint32_t r = (((a >> 16) & 255) * (uint32_t)(255 - t) + ((b >> 16) & 255) * (uint32_t)t) / 255;
	uint32_t gg = (((a >> 8) & 255) * (uint32_t)(255 - t) + ((b >> 8) & 255) * (uint32_t)t) / 255;
	uint32_t bl = ((a & 255) * (uint32_t)(255 - t) + (b & 255) * (uint32_t)t) / 255;
	return RGB(r, gg, bl);
}
