/* monitor - System Monitor: CPU/memory history, task list with scheduling
 * class and priority; select a task and press Kill (or the Delete key). */
#include "gui.h"

#define HIST 90
static int cpu_h[HIST], mem_h[HIST];
static char buf[32768];
static int selected = -1;
static int sel_pid;

int main(void)
{
	struct gwin g;
	if (gwin_open(&g, 780, 560, "System Monitor") < 0)
		return 1;
	struct fu_sysinfo prev;
	sysinfo(&prev);
	struct button kill_btn = { 20, 520, 120, 28, "Kill task", false };
	for (;;) {
		struct fu_sysinfo si;
		sysinfo(&si);
		uint64_t b = si.busy_ns - prev.busy_ns, i = si.idle_ns - prev.idle_ns;
		prev = si;
		memmove(cpu_h, cpu_h + 1, sizeof(int) * (HIST - 1));
		memmove(mem_h, mem_h + 1, sizeof(int) * (HIST - 1));
		cpu_h[HIST - 1] = (int)((b + i) ? b * 100 / (b + i) : 0);
		mem_h[HIST - 1] = (int)((si.mem_total_kb - si.mem_free_kb) * 100 / si.mem_total_kb);

		g_fill(&g, 0, 0, g.w, g.h, T_BG);
		g_text(&g, 20, 14, "CPU", T_DIM);
		g_graph(&g, 20, 34, 350, 90, cpu_h, HIST, T_ACCENT);
		g_text(&g, 400, 14, "Memory", T_DIM);
		g_graph(&g, 400, 34, 350, 90, mem_h, HIST, T_GOOD);
		char line[128];
		snprintf(line, sizeof(line), "%u processes, %u tasks, %u runnable, policy %s, workload %s (%u%%)",
			 si.procs, si.tasks, si.runnable, si.policy, si.system_class, si.class_confidence);
		g_text(&g, 20, 134, line, T_FG);

		read_file("/proc/tasks", buf, sizeof(buf));
		char *save, *l = strtok_r(buf, "\n", &save);
		int row = 0;
		while (l && row < 21) {
			int y = 160 + row * 17;
			if (row == 0) {
				g_text(&g, 20, y, l, T_DIM);
			} else {
				if (row == selected) {
					g_fill(&g, 16, y - 1, g.w - 32, 17, RGB(0x2f, 0x3b, 0x55));
					sel_pid = atoi(l + 6); /* PID column */
				}
				g_text(&g, 20, y, l, T_FG);
			}
			row++;
			l = strtok_r(NULL, "\n", &save);
		}
		kill_btn.active = selected > 0;
		g_button(&g, &kill_btn);
		gwin_present(&g);

		struct fu_wevent ev;
		uint64_t until = uptime_ns() + 1000000000ULL;
		while (uptime_ns() < until) {
			if (gwin_event(&g, &ev, 200) < 0)
				break;
			if (ev.type == WEV_CLOSE) {
				gwin_close(&g);
				return 0;
			}
			if (ev.type == WEV_MOUSE_BUTTON && ev.value == 1) {
				if (button_hit(&kill_btn, ev.x, ev.y) && sel_pid > 1) {
					kill(sel_pid);
					selected = -1;
				} else if (ev.y >= 160 + 17 && ev.y < 160 + 21 * 17) {
					selected = (ev.y - 160) / 17;
				}
				break;
			}
			if (ev.type == WEV_KEY && ev.value && ev.key == K_DELETE && sel_pid > 1) {
				kill(sel_pid);
				selected = -1;
				break;
			}
		}
	}
}
