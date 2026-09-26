/* files - file manager: browse directories, open files in the editor, create
 * folders, delete entries, jump Home/Up. Double-click (or Enter) opens. */
#include "gui.h"

#define MAXE 256
static struct dirent ents[MAXE];
static uint64_t sizes[MAXE];
static int nents, sel, top;
static char cwd[256] = "/home";
static char status[128];

static void load(void)
{
	nents = 0;
	int fd = open(cwd, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		snprintf(status, sizeof(status), "cannot open %s: %s", cwd, strerror(fd));
		return;
	}
	for (long i = 0; nents < MAXE && readdir(fd, i, &ents[nents]) > 0; i++) {
		char full[320];
		snprintf(full, sizeof(full), "%s/%s", strcmp(cwd, "/") ? cwd : "", ents[nents].name);
		struct stat st;
		sizes[nents] = stat(full, &st) == 0 ? st.size : 0;
		nents++;
	}
	close(fd);
	/* directories first */
	for (int i = 0; i < nents; i++)
		for (int j = i + 1; j < nents; j++)
			if ((ents[j].type == VT_DIR) > (ents[i].type == VT_DIR)) {
				struct dirent t = ents[i]; ents[i] = ents[j]; ents[j] = t;
				uint64_t s = sizes[i]; sizes[i] = sizes[j]; sizes[j] = s;
			}
	sel = 0;
	top = 0;
}

static void go(const char *name)
{
	char next[256];
	if (!strcmp(name, ".."))
		snprintf(next, sizeof(next), "%s/..", cwd);
	else if (name[0] == '/')
		strlcpy(next, name, sizeof(next));
	else
		snprintf(next, sizeof(next), "%s/%s", strcmp(cwd, "/") ? cwd : "", name);
	if (chdir(next) == 0)
		getcwd(cwd, sizeof(cwd));
	load();
}

static void open_entry(int i)
{
	if (i < 0 || i >= nents)
		return;
	if (ents[i].type == VT_DIR) {
		go(ents[i].name);
		return;
	}
	char full[320];
	snprintf(full, sizeof(full), "%s/%s", strcmp(cwd, "/") ? cwd : "", ents[i].name);
	const char *argv[] = { "/bin/edit", full, NULL };
	spawn("/bin/edit", argv, NULL);
	snprintf(status, sizeof(status), "opened %s in the editor", ents[i].name);
}

int main(void)
{
	struct gwin g;
	if (gwin_open(&g, 640, 480, "Files") < 0)
		return 1;
	chdir(cwd);
	load();
	struct button bup = { 10, 8, 56, 26, "Up", false }, bhome = { 72, 8, 64, 26, "Home", false },
		      bnew = { 142, 8, 104, 26, "New folder", false }, bdel = { 252, 8, 72, 26, "Delete", false },
		      bterm = { 330, 8, 90, 26, "Terminal", false };
	uint64_t last_click = 0;
	int last_sel = -1;
	for (;;) {
		g_fill(&g, 0, 0, g.w, g.h, T_BG);
		g_fill(&g, 0, 0, g.w, 42, T_BG2);
		g_button(&g, &bup);
		g_button(&g, &bhome);
		g_button(&g, &bnew);
		g_button(&g, &bdel);
		g_button(&g, &bterm);
		g_text(&g, 12, 50, cwd, T_ACCENT);
		int rows = (g.h - 100) / 20;
		for (int r = 0; r < rows && top + r < nents; r++) {
			int i = top + r, y = 74 + r * 20;
			if (i == sel)
				g_fill(&g, 6, y - 2, g.w - 12, 20, RGB(0x2f, 0x3b, 0x55));
			bool dir = ents[i].type == VT_DIR;
			g_round(&g, 14, y + 2, 12, 12, 3, dir ? T_WARN : T_DIM);
			g_text(&g, 34, y, ents[i].name, dir ? T_FG : RGB(0xc8, 0xd0, 0xde));
			char sz[32];
			if (dir)
				strlcpy(sz, "folder", sizeof(sz));
			else if (sizes[i] >= 1024 * 1024)
				snprintf(sz, sizeof(sz), "%lu MiB", sizes[i] >> 20);
			else if (sizes[i] >= 1024)
				snprintf(sz, sizeof(sz), "%lu KiB", sizes[i] >> 10);
			else
				snprintf(sz, sizeof(sz), "%lu B", sizes[i]);
			g_text(&g, g.w - 120, y, sz, T_DIM);
		}
		g_text(&g, 12, g.h - 22, status[0] ? status : "double-click to open; Delete removes", T_DIM);
		gwin_present(&g);

		struct fu_wevent ev;
		if (gwin_event(&g, &ev, 0) < 0)
			continue;
		status[0] = 0;
		if (ev.type == WEV_CLOSE) {
			gwin_close(&g);
			return 0;
		}
		if (ev.type == WEV_MOUSE_BUTTON && ev.value == 1) {
			if (button_hit(&bup, ev.x, ev.y)) go("..");
			else if (button_hit(&bhome, ev.x, ev.y)) go("/home");
			else if (button_hit(&bnew, ev.x, ev.y)) {
				char name[64];
				for (int k = 1; k < 100; k++) {
					snprintf(name, sizeof(name), "New folder %d", k);
					struct stat st;
					if (stat(name, &st) < 0)
						break;
				}
				mkdir(name);
				load();
			} else if (button_hit(&bdel, ev.x, ev.y) && sel < nents) {
				int r = unlink(ents[sel].name);
				snprintf(status, sizeof(status), r < 0 ? "delete failed: %s" : "deleted", strerror(r));
				load();
			} else if (button_hit(&bterm, ev.x, ev.y)) {
				sys2(SYS_DESKTOP_CTL, 4, 0);
			} else if (ev.y >= 72) {
				int i = top + (ev.y - 72) / 20;
				if (i < nents) {
					uint64_t now = uptime_ns();
					if (i == last_sel && now - last_click < 500000000ULL)
						open_entry(i);
					sel = i;
					last_sel = i;
					last_click = now;
				}
			}
		} else if (ev.type == WEV_SCROLL) {
			top = MAX(0, MIN(top + ev.value * 3, nents - 1));
		} else if (ev.type == WEV_KEY && ev.value) {
			if (ev.key == K_DOWN && sel < nents - 1) sel++;
			else if (ev.key == K_UP && sel > 0) sel--;
			else if (ev.ch == '\n') open_entry(sel);
			else if (ev.key == K_BACKSPACE) go("..");
			int rows2 = (g.h - 100) / 20;
			if (sel < top) top = sel;
			if (sel >= top + rows2) top = sel - rows2 + 1;
		}
	}
}
