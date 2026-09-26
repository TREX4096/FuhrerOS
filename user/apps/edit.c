/* edit [FILE] - text editor: arrows/Home/End/PgUp/PgDn move, Enter/Backspace/
 * Delete edit, Ctrl+S saves, Esc or the close button quits. */
#include "gui.h"

#define MAXL 2000
#define LINE 256

static char *text[MAXL];
static int nlines = 1, cy, cx, top;
static char path[256] = "/home/notes.txt";
static bool modified;
static char status[96];

static void load(void)
{
	for (int i = 0; i < MAXL; i++)
		text[i] = NULL;
	text[0] = calloc(LINE, 1);
	nlines = 1;
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		snprintf(status, sizeof(status), "new file");
		return;
	}
	char buf[4096];
	ssize_t n;
	int col = 0;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		for (ssize_t i = 0; i < n; i++) {
			if (buf[i] == '\n') {
				if (nlines < MAXL)
					text[nlines++] = calloc(LINE, 1);
				col = 0;
			} else if (col < LINE - 1 && buf[i] != '\r') {
				text[nlines - 1][col++] = buf[i];
			}
		}
	close(fd);
	if (nlines > 1 && !text[nlines - 1][0])
		nlines--;
	snprintf(status, sizeof(status), "%d lines", nlines);
}

static void save(void)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
	if (fd < 0) {
		snprintf(status, sizeof(status), "save failed: %s", strerror(fd));
		return;
	}
	for (int i = 0; i < nlines; i++) {
		write(fd, text[i], strlen(text[i]));
		write(fd, "\n", 1);
	}
	close(fd);
	sync();
	modified = false;
	snprintf(status, sizeof(status), "saved %d lines", nlines);
}

static void insert_line(int at)
{
	if (nlines >= MAXL)
		return;
	memmove(&text[at + 1], &text[at], sizeof(char *) * (size_t)(nlines - at));
	text[at] = calloc(LINE, 1);
	nlines++;
}

static void key(const struct fu_wevent *ev)
{
	int len = (int)strlen(text[cy]);
	if (cx > len)
		cx = len;
	if ((ev->mods & M_CTRL) && (ev->key == 's' || ev->ch == 19)) {
		save();
		return;
	}
	switch (ev->key) {
	case K_UP: if (cy > 0) cy--; return;
	case K_DOWN: if (cy < nlines - 1) cy++; return;
	case K_LEFT: if (cx > 0) cx--; else if (cy > 0) { cy--; cx = (int)strlen(text[cy]); } return;
	case K_RIGHT: if (cx < len) cx++; else if (cy < nlines - 1) { cy++; cx = 0; } return;
	case K_HOME: cx = 0; return;
	case K_END: cx = len; return;
	case K_PGUP: cy = MAX(0, cy - 20); return;
	case K_PGDN: cy = MIN(nlines - 1, cy + 20); return;
	case K_DELETE:
		if (cx < len)
			memmove(text[cy] + cx, text[cy] + cx + 1, (size_t)(len - cx));
		else if (cy < nlines - 1 && len + (int)strlen(text[cy + 1]) < LINE - 1) {
			strlcat(text[cy], text[cy + 1], LINE);
			free(text[cy + 1]);
			memmove(&text[cy + 1], &text[cy + 2], sizeof(char *) * (size_t)(nlines - cy - 2));
			nlines--;
		}
		modified = true;
		return;
	}
	if (ev->ch == '\n') {
		insert_line(cy + 1);
		strlcpy(text[cy + 1], text[cy] + cx, LINE);
		text[cy][cx] = 0;
		cy++;
		cx = 0;
		modified = true;
	} else if (ev->ch == 8) {
		if (cx > 0) {
			memmove(text[cy] + cx - 1, text[cy] + cx, (size_t)(len - cx + 1));
			cx--;
		} else if (cy > 0 && (int)strlen(text[cy - 1]) + len < LINE - 1) {
			cx = (int)strlen(text[cy - 1]);
			strlcat(text[cy - 1], text[cy], LINE);
			free(text[cy]);
			memmove(&text[cy], &text[cy + 1], sizeof(char *) * (size_t)(nlines - cy - 1));
			nlines--;
			cy--;
		}
		modified = true;
	} else if (ev->ch >= 32 && ev->ch < 127 && len < LINE - 2) {
		memmove(text[cy] + cx + 1, text[cy] + cx, (size_t)(len - cx + 1));
		text[cy][cx++] = (char)ev->ch;
		modified = true;
	} else if (ev->ch == '\t' && len < LINE - 6) {
		for (int k = 0; k < 4; k++) {
			memmove(text[cy] + cx + 1, text[cy] + cx, (size_t)(strlen(text[cy]) - (size_t)cx + 1));
			text[cy][cx++] = ' ';
		}
		modified = true;
	}
}

int main(int argc, char **argv)
{
	if (argc > 1)
		strlcpy(path, argv[1], sizeof(path));
	struct gwin g;
	char title[96];
	snprintf(title, sizeof(title), "Editor - %s", path);
	if (gwin_open(&g, 700, 480, title) < 0)
		return 1;
	load();
	for (;;) {
		int rows = (g.h - 44) / FONT_H;
		if (cy < top)
			top = cy;
		if (cy >= top + rows)
			top = cy - rows + 1;
		g_fill(&g, 0, 0, g.w, g.h, T_BG);
		for (int r = 0; r < rows && top + r < nlines; r++) {
			char num[8];
			snprintf(num, sizeof(num), "%4d", top + r + 1);
			g_text(&g, 4, 6 + r * FONT_H, num, T_DIM);
			g_text(&g, 48, 6 + r * FONT_H, text[top + r], T_FG);
		}
		int ccx = MIN(cx, (int)strlen(text[cy]));
		g_fill(&g, 48 + ccx * FONT_W, 6 + (cy - top) * FONT_H, 2, FONT_H, T_ACCENT);
		g_fill(&g, 0, g.h - 26, g.w, 26, T_BG2);
		char st[200];
		snprintf(st, sizeof(st), "%s%s  |  line %d/%d col %d  |  %s  |  Ctrl+S save", path,
			 modified ? " *" : "", cy + 1, nlines, ccx + 1, status);
		g_text(&g, 8, g.h - 21, st, T_DIM);
		gwin_present(&g);
		struct fu_wevent ev;
		if (gwin_event(&g, &ev, 0) < 0)
			continue;
		if (ev.type == WEV_CLOSE || (ev.type == WEV_KEY && ev.value && ev.key == K_ESC)) {
			gwin_close(&g);
			return 0;
		}
		if (ev.type == WEV_KEY && ev.value)
			key(&ev);
		else if (ev.type == WEV_SCROLL)
			cy = MAX(0, MIN(nlines - 1, cy + ev.value * 3));
		else if (ev.type == WEV_MOUSE_BUTTON && ev.value == 1) {
			cy = MIN(nlines - 1, top + (ev.y - 6) / FONT_H);
			cx = MAX(0, (ev.x - 48) / FONT_W);
		}
	}
}
