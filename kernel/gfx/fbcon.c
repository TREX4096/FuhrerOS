/* Framebuffer text console with a tiny ANSI subset (SGR colours, clear,
 * cursor home, erase line). Used from boot until the compositor owns the
 * screen.
 *
 * Text lives in a character-cell grid in RAM (a ring of rows). Scrolling is
 * an index change; rendering only *writes* the framebuffer (reading
 * write-combining video memory is extremely slow) and happens once per
 * console_write call, not once per line (F-104). */
#include "gfx/fb.h"
#include "kernel.h"

#define MARGIN 8
#define MAX_COLS 240
#define MAX_ROWS 100

static const u32 ansi_colors[16] = {
	RGB(0x1d, 0x23, 0x30), RGB(0xe0, 0x6c, 0x75), RGB(0x98, 0xc3, 0x79), RGB(0xe5, 0xc0, 0x7b),
	RGB(0x61, 0xaf, 0xef), RGB(0xc6, 0x78, 0xdd), RGB(0x56, 0xb6, 0xc2), RGB(0xdc, 0xdf, 0xe4),
	RGB(0x5c, 0x63, 0x70), RGB(0xff, 0x7b, 0x86), RGB(0xb5, 0xe8, 0x90), RGB(0xff, 0xd8, 0x8a),
	RGB(0x82, 0xc8, 0xff), RGB(0xe3, 0x9a, 0xf5), RGB(0x7f, 0xdb, 0xe6), RGB(0xff, 0xff, 0xff),
};
#define DEFAULT_FG 7
#define BG_COLOR RGB(0x14, 0x18, 0x22)

struct cell {
	u8 ch;
	u8 fg;	/* palette index */
};

static struct cell grid[MAX_ROWS][MAX_COLS];
static bool row_dirty[MAX_ROWS];
static int top_row;		/* ring index of the first visible row */
static struct surface *scr;
static int cols, rows, cx, cy;
static u8 fg = DEFAULT_FG;
static bool active, all_dirty;
static int esc_state, esc_args[4], esc_n;

bool fbcon_active(void) { return active; }
void fbcon_detach(void) { active = false; }
int fbcon_cols(void) { return cols ? cols : 80; }
int fbcon_rows(void) { return rows ? rows : 25; }

void fbcon_set_colors(u32 f, u32 b)
{
	for (int i = 0; i < 16; i++)
		if (ansi_colors[i] == f)
			fg = (u8)i;
}

static struct cell *at(int r, int c) { return &grid[(top_row + r) % rows][c]; }

void fbcon_clear(void)
{
	if (!scr)
		return;
	memset(grid, 0, sizeof(grid));
	top_row = cx = cy = 0;
	all_dirty = true;
}

void fbcon_init(void)
{
	scr = fb_screen();
	if (!scr)
		return;
	cols = MIN(((int)scr->w - 2 * MARGIN) / FONT_W, MAX_COLS);
	rows = MIN(((int)scr->h - 2 * MARGIN) / FONT_H, MAX_ROWS);
	surf_fill(scr, 0, 0, (int)scr->w, (int)scr->h, BG_COLOR);
	fbcon_clear();
	active = true;
}

static void newline(void)
{
	cx = 0;
	if (++cy >= rows) {
		cy = rows - 1;
		top_row = (top_row + 1) % rows;
		memset(&grid[(top_row + rows - 1) % rows][0], 0, sizeof(grid[0]));
		all_dirty = true;
	}
}

static void sgr(void)
{
	for (int i = 0; i < (esc_n ? esc_n : 1); i++) {
		int a = esc_n ? esc_args[i] : 0;
		if (a == 0 || a == 39)
			fg = DEFAULT_FG;
		else if (a == 1 && fg < 8)
			fg = (u8)(fg + 8);
		else if (a >= 30 && a <= 37)
			fg = (u8)(a - 30 + (fg >= 8 ? 8 : 0));
		else if (a >= 90 && a <= 97)
			fg = (u8)(a - 90 + 8);
	}
}

static void putch(char c)
{
	if (esc_state == 1) {
		esc_state = c == '[' ? 2 : 0;
		esc_n = 0;
		esc_args[0] = 0;
		return;
	}
	if (esc_state == 2) {
		if (c >= '0' && c <= '9') {
			if (!esc_n)
				esc_n = 1;
			esc_args[esc_n - 1] = esc_args[esc_n - 1] * 10 + (c - '0');
			return;
		}
		if (c == ';') {
			if (esc_n < 4)
				esc_args[esc_n++] = 0;
			return;
		}
		esc_state = 0;
		if (c == 'm')
			sgr();
		else if (c == 'J')
			fbcon_clear();
		else if (c == 'H')
			cx = cy = 0;
		else if (c == 'K') {
			for (int i = cx; i < cols; i++)
				at(cy, i)->ch = 0;
			row_dirty[cy] = true;
		}
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
			at(cy, cx)->ch = 0;
			row_dirty[cy] = true;
		}
		return;
	}
	struct cell *ce = at(cy, cx);
	ce->ch = (u8)c;
	ce->fg = fg;
	row_dirty[cy] = true;
	if (++cx >= cols)
		newline();
}

static void render_row(int r)
{
	int y = MARGIN + r * FONT_H;
	for (int c = 0; c < cols; c++) {
		struct cell *ce = at(r, c);
		surf_char(scr, MARGIN + c * FONT_W, y, ce->ch ? ce->ch : ' ', ansi_colors[ce->fg & 15],
			  BG_COLOR, false);
	}
	row_dirty[r] = false;
}

void fbcon_write(const char *s, size_t n)
{
	if (!active)
		return;
	for (size_t i = 0; i < n; i++)
		putch(s[i]);
	for (int r = 0; r < rows; r++)
		if (all_dirty || row_dirty[r])
			render_row(r);
	all_dirty = false;
}
