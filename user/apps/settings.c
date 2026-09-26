/* settings - Appearance, Touchpad, Desktop, Kernel, Shortcuts, About
 * (UI_SUGGESTION §17, §20, §22, §28, §29). Every change is applied at once
 * through SYS_DESKTOP_CTL (DESK_SET_CFG) or the scheduler/cache controls. */
#include "gui.h"

static const char *sections[] = { "Appearance", "Touchpad", "Desktop", "Kernel", "Shortcuts", "About" };
#define NSEC 6
static const char *accent_names[] = { "Fuhrer red", "Blue", "Teal", "Amber", "Violet" };
static const char *wall_names[] = { "Graphite", "Midnight", "Ember" };
static const char *gesture_names[] = { "3-finger swipe left", "3-finger swipe right", "3-finger swipe up",
				       "3-finger swipe down", "3-finger tap", "4-finger swipe left",
				       "4-finger swipe right", "4-finger swipe up", "4-finger swipe down",
				       "4-finger tap" };
static const char *action_names[] = { "Nothing", "Previous workspace", "Next workspace", "Overview",
				      "Show desktop", "Previous window", "Next window", "Launcher",
				      "Control Center" };
static const char *pols[] = { "round_robin", "priority", "low_latency", "adaptive" };
static const char *cpols[] = { "lru", "fifo", "clock", "readahead", "adaptive" };
static const int windows_ms[] = { 25, 50, 100, 250, 500 };
static const int hyst[] = { 1, 2, 3, 5 };
static const int typing_ms[] = { 0, 100, 250, 500, 1000 };

static struct fu_desk_cfg cfg;
static int sec;

/* clickable regions collected while drawing */
#define MAXHIT 64
static struct hit {
	int x, y, w, h;
	int kind, a;
} hits[MAXHIT];
static int nhits;
enum { H_SEC, H_DARK, H_ACCENT, H_WALL, H_MOTION, H_TAP, H_NATURAL, H_PSPEED, H_SSPEED, H_TYPING,
       H_GESTURE, H_WS, H_NOTIFY, H_SCHED, H_WINDOW, H_HYST, H_CACHE };

static void add_hit(int x, int y, int w, int h, int kind, int a)
{
	if (nhits < MAXHIT)
		hits[nhits++] = (struct hit){ x, y, w, h, kind, a };
}

static void button(struct gwin *g, int x, int y, int w, const char *label, bool active, int kind, int a)
{
	struct button b = { x, y, w, 28, label, active };
	g_button(g, &b);
	add_hit(x, y, w, 28, kind, a);
}

static void toggle(struct gwin *g, int x, int y, const char *label, bool on, int kind)
{
	g_text(g, x, y + 6, label, T_FG);
	int sx = x + 300;
	g_round(g, sx, y + 2, 44, 24, 12, on ? T_ACCENT : T_BORDER);
	g_round(g, on ? sx + 22 : sx + 2, y + 4, 20, 20, 10, RGB(0xff, 0xff, 0xff));
	g_text(g, sx + 54, y + 6, on ? "On" : "Off", T_DIM);
	add_hit(sx, y, 44, 28, kind, 0);
}

static void stepper(struct gwin *g, int x, int y, const char *label, int v, int kind)
{
	g_text(g, x, y + 6, label, T_FG);
	int sx = x + 300;
	button(g, sx, y, 32, "-", false, kind, -1);
	for (int i = 1; i <= 8; i++)
		g_round(g, sx + 40 + (i - 1) * 16, y + 10, 12, 8, 4, i <= v ? T_ACCENT : T_BORDER);
	button(g, sx + 40 + 8 * 16 + 4, y, 32, "+", false, kind, 1);
}

static void heading(struct gwin *g, int x, int y, const char *t)
{
	g_text(g, x, y, t, T_DIM);
	g_fill(g, x, y + 18, g->w - x - 24, 1, T_BORDER);
}

static void draw(struct gwin *g)
{
	nhits = 0;
	g_fill(g, 0, 0, g->w, g->h, T_BG);
	/* sidebar */
	int sbw = 170;
	g_fill(g, 0, 0, sbw, g->h, T_BG2);
	g_text_big(g, 16, 16, "Settings", T_FG, 2);
	for (int i = 0; i < NSEC; i++) {
		int y = 64 + i * 36;
		if (i == sec)
			g_round(g, 8, y, sbw - 16, 30, 8, g_mix(T_BG2, T_ACCENT, 90));
		g_text(g, 22, y + 7, sections[i], i == sec ? T_FG : T_DIM);
		add_hit(8, y, sbw - 16, 30, H_SEC, i);
	}
	int x = sbw + 24, y = 20;
	char buf[96];
	switch (sec) {
	case 0:
		heading(g, x, y, "Theme");
		button(g, x, y + 28, 100, "Dark", cfg.dark, H_DARK, 1);
		button(g, x + 108, y + 28, 100, "Light", !cfg.dark, H_DARK, 0);
		y += 80;
		heading(g, x, y, "Accent colour");
		for (int i = 0; i < 5; i++)
			button(g, x + i * 112, y + 28, 104, accent_names[i], cfg.accent == i, H_ACCENT, i);
		y += 80;
		heading(g, x, y, "Wallpaper");
		for (int i = 0; i < 3; i++)
			button(g, x + i * 112, y + 28, 104, wall_names[i], cfg.wallpaper == i, H_WALL, i);
		y += 80;
		heading(g, x, y, "Accessibility");
		toggle(g, x, y + 28, "Reduce motion", cfg.reduce_motion, H_MOTION);
		g_text(g, x, y + 64, "FuhrerOS currently draws no animations, so this has no visible effect yet.", T_DIM);
		break;
	case 1:
		heading(g, x, y, "Touchpad");
		toggle(g, x, y + 28, "Tap to click", cfg.tap_to_click, H_TAP);
		toggle(g, x, y + 62, "Natural scrolling", cfg.natural_scroll, H_NATURAL);
		stepper(g, x, y + 96, "Pointer speed", cfg.pointer_speed, H_PSPEED);
		stepper(g, x, y + 130, "Scroll speed", cfg.scroll_speed, H_SSPEED);
		g_text(g, x, y + 170, "Disable touchpad while typing", T_FG);
		for (int i = 0; i < 5; i++) {
			if (typing_ms[i])
				snprintf(buf, sizeof(buf), "%d ms", typing_ms[i]);
			else
				snprintf(buf, sizeof(buf), "Off");
			button(g, x + 300 + i * 74, y + 164, 68, i ? buf : "Off", cfg.typing_block_ms == typing_ms[i],
			       H_TYPING, i);
		}
		y += 210;
		heading(g, x, y, "Gestures (click to change)");
		for (int i = 0; i < 10; i++) {
			int gy = y + 26 + i * 30;
			g_text(g, x, gy + 6, gesture_names[i], T_FG);
			button(g, x + 300, gy, 200, action_names[cfg.gesture[i] % GA_COUNT], false, H_GESTURE, i);
		}
		g_text(g, x, y + 334, "QEMU has no multi-touch device: gestures are tested with synthetic frames.", T_DIM);
		break;
	case 2:
		heading(g, x, y, "Workspaces");
		snprintf(buf, sizeof(buf), "%d workspaces", cfg.workspaces);
		g_text(g, x, y + 34, buf, T_FG);
		button(g, x + 300, y + 28, 32, "-", false, H_WS, -1);
		button(g, x + 340, y + 28, 32, "+", false, H_WS, 1);
		for (int i = 0; i < cfg.workspaces; i++) {
			snprintf(buf, sizeof(buf), "%d  %s", i + 1, cfg.ws_name[i]);
			g_text(g, x + 16, y + 70 + i * 18, buf, T_DIM);
		}
		g_text(g, x, y + 76 + cfg.workspaces * 18, "Rename: open the launcher and type  rename NAME", T_DIM);
		y += 120 + cfg.workspaces * 18;
		heading(g, x, y, "Notifications");
		toggle(g, x, y + 28, "Notify on workload changes", cfg.adapt_notify, H_NOTIFY);
		g_text(g, x, y + 62, "Off by default: policy changes are always listed in the Control Center.", T_DIM);
		break;
	case 3: {
		char sp[32] = "", cp[32] = "";
		sched_ctl(SCHED_GET_POLICY, 0, sp);
		static char bcb[2048], sb[4096];
		read_file("/proc/bcache", bcb, sizeof(bcb));
		read_file("/proc/sched", sb, sizeof(sb));
		text_value(bcb, "policy", cp, sizeof(cp));
		long win = text_num(sb, "window_ms"), hy = text_num(sb, "hysteresis");
		heading(g, x, y, "Scheduler policy");
		for (int i = 0; i < 4; i++)
			button(g, x + i * 132, y + 28, 124, pols[i], !strcmp(sp, pols[i]), H_SCHED, i);
		y += 80;
		heading(g, x, y, "Profiler window (how often tasks are re-classified)");
		for (int i = 0; i < 5; i++) {
			snprintf(buf, sizeof(buf), "%d ms", windows_ms[i]);
			button(g, x + i * 90, y + 28, 82, buf, win == windows_ms[i], H_WINDOW, i);
		}
		y += 80;
		heading(g, x, y, "Hysteresis (windows before a class change)");
		for (int i = 0; i < 4; i++) {
			snprintf(buf, sizeof(buf), "%d", hyst[i]);
			button(g, x + i * 60, y + 28, 52, buf, hy == hyst[i], H_HYST, i);
		}
		y += 80;
		heading(g, x, y, "Buffer cache policy");
		for (int i = 0; i < 5; i++)
			button(g, x + i * 104, y + 28, 96, cpols[i], !strcmp(cp, cpols[i]), H_CACHE, i);
		g_text(g, x, y + 76, "Results for these settings: docs/experiments.md (E-117..E-120).", T_DIM);
		break;
	}
	case 4: {
		static const char *sc[] = {
			"Super                 launcher / command palette",
			"Alt+Tab               window switcher (hold Alt)",
			"Super+Tab, Super+O    overview",
			"Super+T               layout: floating -> tiling -> focus",
			"Super+Left/Right      snap (floating) / focus neighbour (tiling)",
			"Super+Shift+Left/Right  swap tiles",
			"Super+[  Super+]      shrink / grow the master tile",
			"Super+R               rotate tiling orientation",
			"Super+Space           float / re-tile the window",
			"Super+Up / Down       maximise / restore-minimise",
			"Super+D               show desktop",
			"Super+L               lock screen",
			"Super+F               Fuhrer Control Center",
			"Super+E               files",
			"Ctrl+Alt+T            terminal",
			"Super+1..9            workspace",
			"Super+Shift+1..9      move window to workspace (and follow)",
			"Super+Ctrl+Left/Right previous / next workspace",
			"Super+Shift+Q         close window",
		};
		heading(g, x, y, "Keyboard shortcuts");
		for (unsigned i = 0; i < sizeof(sc) / sizeof(sc[0]); i++)
			g_text(g, x, y + 28 + (int)i * 20, sc[i], T_FG);
		break;
	}
	case 5: {
		static char ver[512];
		read_file("/proc/version", ver, sizeof(ver));
		heading(g, x, y, "About");
		g_text_big(g, x, y + 30, "FuhrerOS", T_FG, 3);
		g_text(g, x, y + 86, "An x86-64 operating system with its own kernel, written from scratch.", T_FG);
		char *save, *l = strtok_r(ver, "\n", &save);
		for (int i = 0; l && i < 8; i++) {
			g_text(g, x, y + 116 + i * 18, l, T_DIM);
			l = strtok_r(NULL, "\n", &save);
		}
		break;
	}
	}
	gwin_present(g);
}

static void set_cfg(void) { sys2(SYS_DESKTOP_CTL, DESK_SET_CFG, &cfg); }

static void clicked(struct hit *h)
{
	switch (h->kind) {
	case H_SEC: sec = h->a; return;
	case H_DARK: cfg.dark = (uint8_t)h->a; break;
	case H_ACCENT: cfg.accent = (uint8_t)h->a; break;
	case H_WALL: cfg.wallpaper = (uint8_t)h->a; break;
	case H_MOTION: cfg.reduce_motion ^= 1; break;
	case H_TAP: cfg.tap_to_click ^= 1; break;
	case H_NATURAL: cfg.natural_scroll ^= 1; break;
	case H_PSPEED: cfg.pointer_speed = (uint8_t)MIN(MAX(cfg.pointer_speed + h->a, 1), 8); break;
	case H_SSPEED: cfg.scroll_speed = (uint8_t)MIN(MAX(cfg.scroll_speed + h->a, 1), 8); break;
	case H_TYPING: cfg.typing_block_ms = (uint16_t)typing_ms[h->a]; break;
	case H_GESTURE: cfg.gesture[h->a] = (uint8_t)((cfg.gesture[h->a] + 1) % GA_COUNT); break;
	case H_WS: cfg.workspaces = (uint8_t)MIN(MAX(cfg.workspaces + h->a, 1), DESK_MAX_WS); break;
	case H_NOTIFY: cfg.adapt_notify ^= 1; break;
	case H_SCHED: sched_ctl(SCHED_SET_POLICY, 0, (char *)pols[h->a]); return;
	case H_WINDOW: sched_ctl(SCHED_SET_WINDOW, windows_ms[h->a], NULL); return;
	case H_HYST: sched_ctl(SCHED_SET_HYSTERESIS, hyst[h->a], NULL); return;
	case H_CACHE: sys3(SYS_CACHE_CTL, CACHE_SET_POLICY, 0, cpols[h->a]); return;
	}
	set_cfg();
}

int main(int argc, char **argv)
{
	struct gwin g;
	if (argc > 1)
		for (int i = 0; i < NSEC; i++)
			if (!strcmp(argv[1], sections[i]) || (argv[1][0] | 32) == (sections[i][0] | 32))
				sec = i;
	if (gwin_open(&g, 860, 600, "Settings") < 0)
		return 1;
	sys2(SYS_DESKTOP_CTL, DESK_GET_CFG, &cfg);
	for (;;) {
		draw(&g);
		struct fu_wevent ev;
		if (gwin_event(&g, &ev, 0) < 0)
			continue;
		if (ev.type == WEV_CLOSE) {
			gwin_close(&g);
			return 0;
		}
		if (ev.type == WEV_KEY && ev.value && (ev.key == K_UP || ev.key == K_DOWN)) {
			sec = (sec + (ev.key == K_UP ? NSEC - 1 : 1)) % NSEC;
			continue;
		}
		if (ev.type != WEV_MOUSE_BUTTON || ev.value != 1)
			continue;
		for (int i = 0; i < nhits; i++)
			if (ev.x >= hits[i].x && ev.x < hits[i].x + hits[i].w && ev.y >= hits[i].y &&
			    ev.y < hits[i].y + hits[i].h) {
				clicked(&hits[i]);
				sys2(SYS_DESKTOP_CTL, DESK_GET_CFG, &cfg);
				break;
			}
	}
}
