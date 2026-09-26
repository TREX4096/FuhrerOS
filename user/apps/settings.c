/* settings - appearance, scheduler/profiler parameters, cache policy, and
 * a keyboard-shortcut reference. Changes apply immediately. */
#include "gui.h"

static const char *themes[] = { "Dusk", "Teal", "Ember" };
static const char *pols[] = { "round_robin", "priority", "low_latency", "adaptive" };
static const int windows_ms[] = { 25, 50, 100, 250, 500 };
static const int hyst[] = { 1, 2, 3, 5 };
static int theme, win_idx = 2, hyst_idx = 1;

int main(void)
{
	struct gwin g;
	if (gwin_open(&g, 640, 540, "Settings") < 0)
		return 1;
	struct button tb[3], pb[4], wb[5], hb[4];
	for (;;) {
		char sp[32] = "";
		sched_ctl(SCHED_GET_POLICY, 0, sp);
		g_fill(&g, 0, 0, g.w, g.h, T_BG);
		g_text_big(&g, 20, 14, "Settings", T_FG, 2);
		int y = 60;
		g_text(&g, 20, y, "Wallpaper", T_DIM);
		for (int i = 0; i < 3; i++) {
			tb[i] = (struct button){ 20 + i * 110, y + 20, 100, 28, themes[i], i == theme };
			g_button(&g, &tb[i]);
		}
		y += 70;
		g_text(&g, 20, y, "Scheduler policy", T_DIM);
		for (int i = 0; i < 4; i++) {
			pb[i] = (struct button){ 20 + i * 150, y + 20, 140, 28, pols[i], !strcmp(sp, pols[i]) };
			g_button(&g, &pb[i]);
		}
		y += 70;
		g_text(&g, 20, y, "Profiler window (how often tasks are re-classified)", T_DIM);
		char lbl[5][12];
		for (int i = 0; i < 5; i++) {
			snprintf(lbl[i], sizeof(lbl[i]), "%d ms", windows_ms[i]);
			wb[i] = (struct button){ 20 + i * 90, y + 20, 80, 28, lbl[i], i == win_idx };
			g_button(&g, &wb[i]);
		}
		y += 70;
		g_text(&g, 20, y, "Hysteresis (windows before a class change)", T_DIM);
		char hl[4][8];
		for (int i = 0; i < 4; i++) {
			snprintf(hl[i], sizeof(hl[i]), "%d", hyst[i]);
			hb[i] = (struct button){ 20 + i * 60, y + 20, 50, 28, hl[i], i == hyst_idx };
			g_button(&g, &hb[i]);
		}
		y += 70;
		g_text(&g, 20, y, "Keyboard shortcuts", T_DIM);
		static const char *sc[] = {
			"Super            launcher            Super+Tab / Alt+Tab   switch windows",
			"Super+Left/Right snap halves         Super+Up / Down       maximise / minimise",
			"Super+D          show desktop        Super+L               lock screen",
			"Super+O          overview            Super+T               toggle tiling",
			"Super+1..4       workspace           Super+Shift+1..4      move window",
			"Ctrl+Alt+T       terminal            Super+E               files",
		};
		for (int i = 0; i < 6; i++)
			g_text(&g, 20, y + 22 + i * 18, sc[i], T_FG);
		gwin_present(&g);

		struct fu_wevent ev;
		if (gwin_event(&g, &ev, 0) < 0)
			continue;
		if (ev.type == WEV_CLOSE) {
			gwin_close(&g);
			return 0;
		}
		if (ev.type != WEV_MOUSE_BUTTON || ev.value != 1)
			continue;
		for (int i = 0; i < 3; i++)
			if (button_hit(&tb[i], ev.x, ev.y)) {
				theme = i;
				sys2(SYS_DESKTOP_CTL, 2, i);
			}
		for (int i = 0; i < 4; i++)
			if (button_hit(&pb[i], ev.x, ev.y))
				sched_ctl(SCHED_SET_POLICY, 0, (char *)pols[i]);
		for (int i = 0; i < 5; i++)
			if (button_hit(&wb[i], ev.x, ev.y)) {
				win_idx = i;
				sched_ctl(SCHED_SET_WINDOW, windows_ms[i], NULL);
			}
		for (int i = 0; i < 4; i++)
			if (button_hit(&hb[i], ev.x, ev.y)) {
				hyst_idx = i;
				sched_ctl(SCHED_SET_HYSTERESIS, hyst[i], NULL);
			}
	}
}
