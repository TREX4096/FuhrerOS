/* term - FuhrerOS terminal emulator. Runs `sh -i` connected through pipes,
 * does line editing locally, renders a character grid with scrollback and a
 * subset of ANSI (SGR colours, clear, cursor home, erase line). */
#include "gui.h"

#define COLS_MAX 160
#define ROWS_MAX 60
#define SCROLLBACK 400

struct cell {
	char ch;
	uint8_t fg;
};

static struct cell lines[SCROLLBACK][COLS_MAX];
static int top_line, cur_line, cur_col;	/* ring indices */
static int cols = 90, rows = 26;
static int fg = 7;
static volatile bool dirty = true;
static int to_shell, from_shell;
static char input[256];
static int input_len;
static int scroll_off;

static const uint32_t palette[16] = {
	RGB(0x1d, 0x23, 0x30), RGB(0xe0, 0x6c, 0x75), RGB(0x98, 0xc3, 0x79), RGB(0xe5, 0xc0, 0x7b),
	RGB(0x61, 0xaf, 0xef), RGB(0xc6, 0x78, 0xdd), RGB(0x56, 0xb6, 0xc2), RGB(0xdc, 0xdf, 0xe4),
	RGB(0x5c, 0x63, 0x70), RGB(0xff, 0x7b, 0x86), RGB(0xb5, 0xe8, 0x90), RGB(0xff, 0xd8, 0x8a),
	RGB(0x82, 0xc8, 0xff), RGB(0xe3, 0x9a, 0xf5), RGB(0x7f, 0xdb, 0xe6), RGB(0xff, 0xff, 0xff),
};

static void newline(void)
{
	cur_col = 0;
	cur_line = (cur_line + 1) % SCROLLBACK;
	memset(lines[cur_line], 0, sizeof(lines[cur_line]));
	int used = (cur_line - top_line + SCROLLBACK) % SCROLLBACK;
	if (used >= SCROLLBACK - 1)
		top_line = (top_line + 1) % SCROLLBACK;
}

static void clear_screen(void)
{
	memset(lines, 0, sizeof(lines));
	top_line = cur_line = cur_col = 0;
}

static int esc, esc_n, esc_args[4];

static void put(char c)
{
	if (esc == 1) {
		esc = c == '[' ? 2 : 0;
		esc_n = 0;
		esc_args[0] = 0;
		return;
	}
	if (esc == 2) {
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
		esc = 0;
		if (c == 'm') {
			for (int i = 0; i < (esc_n ? esc_n : 1); i++) {
				int a = esc_n ? esc_args[i] : 0;
				if (a == 0) fg = 7;
				else if (a == 1 && fg < 8) fg += 8;
				else if (a >= 30 && a <= 37) fg = a - 30 + (fg >= 8 ? 8 : 0);
				else if (a >= 90 && a <= 97) fg = a - 90 + 8;
			}
		} else if (c == 'J') {
			clear_screen();
		} else if (c == 'H') {
			cur_col = 0;
		} else if (c == 'K') {
			for (int i = cur_col; i < COLS_MAX; i++)
				lines[cur_line][i].ch = 0;
		}
		return;
	}
	switch (c) {
	case 27: esc = 1; return;
	case '\n': newline(); return;
	case '\r': cur_col = 0; return;
	case '\b': if (cur_col) cur_col--; return;
	case '\t': cur_col = (cur_col + 8) & ~7; if (cur_col >= cols) newline(); return;
	}
	if ((unsigned char)c < 32)
		return;
	if (cur_col >= cols)
		newline();
	lines[cur_line][cur_col].ch = c;
	lines[cur_line][cur_col].fg = (uint8_t)fg;
	cur_col++;
}

static void reader(void *arg)
{
	char buf[1024];
	for (;;) {
		ssize_t n = read(from_shell, buf, sizeof(buf));
		if (n <= 0) {
			const char *m = "\n[shell exited - close this window]\n";
			for (const char *p = m; *p; p++)
				put(*p);
			dirty = true;
			return;
		}
		for (ssize_t i = 0; i < n; i++)
			put(buf[i]);
		dirty = true;
	}
}

static void render(struct gwin *g)
{
	g_fill(g, 0, 0, g->w, g->h, RGB(0x12, 0x16, 0x20));
	int used = (cur_line - top_line + SCROLLBACK) % SCROLLBACK + 1;
	int first = used > rows ? used - rows - scroll_off : 0;
	if (first < 0)
		first = 0;
	for (int r = 0; r < rows && first + r < used; r++) {
		int li = (top_line + first + r) % SCROLLBACK;
		for (int c = 0; c < cols && c < COLS_MAX; c++) {
			struct cell *ce = &lines[li][c];
			if (ce->ch)
				g_char(g, 6 + c * FONT_W, 4 + r * FONT_H, (unsigned char)ce->ch,
				       palette[ce->fg & 15], 0, true);
		}
	}
	/* pending input + cursor after the last output on the current line */
	int crow = (cur_line - top_line + SCROLLBACK) % SCROLLBACK - first;
	if (crow >= 0 && crow < rows && !scroll_off) {
		int x = 6 + cur_col * FONT_W, y = 4 + crow * FONT_H;
		g_textn(g, x, y, input, input_len, palette[15]);
		g_fill(g, x + input_len * FONT_W, y + FONT_H - 3, FONT_W, 2, T_ACCENT);
	}
	gwin_present(g);
}

int main(void)
{
	struct gwin g;
	if (gwin_open(&g, cols * FONT_W + 12, rows * FONT_H + 8, "Terminal") < 0) {
		dprintf(2, "term: desktop not running\n");
		return 1;
	}
	int in[2], out[2];
	pipe(in);
	pipe(out);
	to_shell = in[1];
	from_shell = out[0];
	const char *argv[] = { "/bin/sh", "-i", NULL };
	int stdio[3] = { in[0], out[1], out[1] };
	pid_t sh = spawn("/bin/sh", argv, stdio);
	close(in[0]);
	close(out[1]);
	thread_create(reader, NULL, 0);
	render(&g);
	for (;;) {
		struct fu_wevent ev;
		gwin_event(&g, &ev, 30);
		if (ev.type == WEV_CLOSE) {
			kill(sh);
			gwin_close(&g);
			return 0;
		}
		if (ev.type == WEV_RESIZE) {
			cols = MIN((g.w - 12) / FONT_W, COLS_MAX);
			rows = MIN((g.h - 8) / FONT_H, ROWS_MAX);
			dirty = true;
		} else if (ev.type == WEV_SCROLL) {
			scroll_off = MAX(0, scroll_off + ev.value * 3);
			dirty = true;
		} else if (ev.type == WEV_KEY && ev.value) {
			scroll_off = 0;
			if (ev.ch == '\n' || ev.ch == '\r') {
				input[input_len++] = '\n';
				for (int i = 0; i < input_len - 1; i++)
					put(input[i]); /* echo the committed line */
				write(to_shell, input, (size_t)input_len);
				newline();
				input_len = 0;
			} else if (ev.ch == 8) {
				if (input_len)
					input_len--;
			} else if (ev.ch == 3) {
				const char *m = "^C\n";
				for (const char *p = m; *p; p++)
					put(*p);
				input_len = 0;
				write(to_shell, "\n", 1);
			} else if (ev.ch == 12) {
				clear_screen();
			} else if (ev.ch >= 32 && ev.ch < 127 && input_len < (int)sizeof(input) - 2) {
				input[input_len++] = (char)ev.ch;
			}
			dirty = true;
		}
		if (dirty) {
			dirty = false;
			render(&g);
		}
	}
}
