/* control - Fuhrer Control Center (NEW_EXPLANATION §33, UI_SUGGESTION §11/§36).
 * Makes the adaptive kernel visible: resources, the detected workload with
 * confidence and reason, what the scheduler / cache / network are doing
 * about it, system-level transitions, profiler overhead, per-task classes,
 * and switchable policies. The layout reflows to the window width. */
#include "gui.h"

#define HIST 60
static int cpu_hist[HIST];
static char procbuf[32768], adapt[16384], bc[4096], schedb[4096];

static const char *sched_pols[] = { "round_robin", "priority", "low_latency", "adaptive" };
static const char *sched_labels[] = { "Round robin", "Priority", "Low latency", "Adaptive" };
static const char *cache_pols[] = { "lru", "fifo", "clock", "readahead", "adaptive" };
static struct button sbtn[4], cbtn[5];

/* adaptive.cpp: class -> priority / quantum */
static const struct {
	const char *cls, *what;
} class_params[] = {
	{ "INTERACTIVE", "priority 4, 3 ms quantum, preempts on wake" },
	{ "IO_BOUND", "priority 8, 5 ms quantum, preempts on wake" },
	{ "CPU_BOUND", "priority 20, 30 ms quantum (fewer switches)" },
	{ "MIXED", "priority 12, 8 ms quantum" },
	{ "IDLE", "priority 12, 8 ms quantum" },
};

static void card(struct gwin *g, int x, int y, int w, int h, const char *title)
{
	g_round(g, x, y, w, h, 10, T_BG2);
	g_text(g, x + 14, y + 10, title, T_DIM);
}

static void fmt_rate(char *b, int n, uint64_t v)
{
	if (v >= 1048576)
		snprintf(b, (size_t)n, "%lu.%lu MB/s", v / 1048576, v % 1048576 * 10 / 1048576);
	else
		snprintf(b, (size_t)n, "%lu KB/s", v / 1024);
}

int main(void)
{
	struct gwin g;
	if (gwin_open(&g, 900, 640, "Fuhrer Control Center") < 0)
		return 1;
	struct fu_sysinfo prev;
	sysinfo(&prev);
	struct fu_netinfo nprev;
	netinfo(&nprev);
	struct fu_blkstat bprev = { 0 };
	sys1(SYS_BLKSTAT, &bprev);
	uint64_t tprev = uptime_ns();
	for (;;) {
		struct fu_sysinfo si;
		sysinfo(&si);
		struct fu_netinfo ni;
		netinfo(&ni);
		struct fu_blkstat bs = { 0 };
		sys1(SYS_BLKSTAT, &bs);
		uint64_t now = uptime_ns(), dt = now - tprev;
		uint64_t busy = si.busy_ns - prev.busy_ns, idle = si.idle_ns - prev.idle_ns;
		int cpu = (int)((busy + idle) ? busy * 100 / (busy + idle) : 0);
		int mem = (int)((si.mem_total_kb - si.mem_free_kb) * 100 / (si.mem_total_kb ? si.mem_total_kb : 1));
		uint64_t rx = dt ? (ni.rx_bytes - nprev.rx_bytes) * 1000000000ULL / dt : 0;
		uint64_t tx = dt ? (ni.tx_bytes - nprev.tx_bytes) * 1000000000ULL / dt : 0;
		uint64_t rd = dt ? (bs.read_bytes - bprev.read_bytes) * 1000000000ULL / dt : 0;
		uint64_t wr = dt ? (bs.write_bytes - bprev.write_bytes) * 1000000000ULL / dt : 0;
		uint64_t cs = dt ? (si.context_switches - prev.context_switches) * 1000000000ULL / dt : 0;
		memmove(cpu_hist, cpu_hist + 1, sizeof(int) * (HIST - 1));
		cpu_hist[HIST - 1] = cpu;
		prev = si;
		nprev = ni;
		bprev = bs;
		tprev = now;
		read_file("/proc/adapt", adapt, sizeof(adapt));
		read_file("/proc/bcache", bc, sizeof(bc));
		read_file("/proc/sched", schedb, sizeof(schedb));

		int W = g.w, pad = 16, two = W >= 860; /* two columns when wide */
		int colw = two ? (W - 3 * pad) / 2 : W - 2 * pad;
		g_fill(&g, 0, 0, W, g.h, T_BG);
		g_text_big(&g, pad, 14, "FUHRER CONTROL CENTER", T_FG, 2);
		char line[200];
		snprintf(line, sizeof(line), "FuhrerOS kernel  |  %s  |  up %lus", si.cpu_brand, si.uptime_ns / 1000000000UL);
		g_textn(&g, pad, 48, line, (W - 2 * pad) / FONT_W, T_DIM);

		/* resources: 2x2 cards */
		int y = 74, cw = (colw - pad) / 2, ch = 64;
		char v[48];
		card(&g, pad, y, cw, ch, "CPU");
		snprintf(v, sizeof(v), "%d%%  %lu cs/s", cpu, cs);
		g_bar(&g, pad + 14, y + 30, cw - 28, 8, cpu, T_ACCENT);
		g_text(&g, pad + 14, y + 42, v, T_FG);
		card(&g, pad + cw + pad, y, cw, ch, "MEMORY");
		snprintf(v, sizeof(v), "%d%%  %lu / %lu MiB", mem, (si.mem_total_kb - si.mem_free_kb) / 1024,
			 si.mem_total_kb / 1024);
		g_bar(&g, pad + cw + pad + 14, y + 30, cw - 28, 8, mem, T_GOOD);
		g_text(&g, pad + cw + pad + 14, y + 42, v, T_FG);
		char a[24], b2[24];
		card(&g, pad, y + ch + 10, cw, ch, "NETWORK");
		fmt_rate(a, sizeof(a), rx);
		fmt_rate(b2, sizeof(b2), tx);
		snprintf(v, sizeof(v), "down %s", a);
		g_text(&g, pad + 14, y + ch + 10 + 28, v, T_FG);
		snprintf(v, sizeof(v), "up   %s", b2);
		g_text(&g, pad + 14, y + ch + 10 + 44, v, T_DIM);
		card(&g, pad + cw + pad, y + ch + 10, cw, ch, "STORAGE");
		fmt_rate(a, sizeof(a), rd);
		fmt_rate(b2, sizeof(b2), wr);
		snprintf(v, sizeof(v), "read  %s", a);
		g_text(&g, pad + cw + pad + 14, y + ch + 10 + 28, v, T_FG);
		snprintf(v, sizeof(v), "write %s", b2);
		g_text(&g, pad + cw + pad + 14, y + ch + 10 + 44, v, T_DIM);
		int gy = y + 2 * ch + 20;
		g_round(&g, pad, gy, colw, 84, 10, T_BG2);
		g_graph(&g, pad + 8, gy + 8, colw - 16, 68, cpu_hist, HIST, T_ACCENT);
		g_text(&g, pad + 14, gy + 10, "CPU, last 60 s", T_DIM);

		/* adaptive kernel panel */
		int ax = two ? pad + colw + pad : pad, ay = two ? 74 : gy + 100, aw = colw;
		char cls[32], reason[96], conf[16], cpol[24], iocls[24], lastt[48];
		text_value(adapt, "stable_class", cls, sizeof(cls));
		text_value(adapt, "reason", reason, sizeof(reason));
		text_value(adapt, "confidence", conf, sizeof(conf));
		text_value(adapt, "last_transition", lastt, sizeof(lastt));
		long ntrans = text_num(adapt, "system_transitions"), ago = text_num(adapt, "last_transition_ms_ago");
		long pov = text_num(adapt, "profiler_overhead_ns");
		text_value(bc, "policy", cpol, sizeof(cpol));
		text_value(bc, "io_class", iocls, sizeof(iocls));
		long hits = text_num(bc, "hit_rate_pm");
		int ah = 290;
		g_round(&g, ax, ay, aw, ah, 10, T_BG2);
		int ty = ay + 12;
		g_text(&g, ax + 14, ty, "WORKLOAD", T_DIM);
		g_text_big(&g, ax + 14, ty + 18, cls, T_ACCENT, 2);
		snprintf(line, sizeof(line), "confidence %s%%  -  %s", conf, reason);
		g_textn(&g, ax + 14, ty + 52, line, (aw - 28) / FONT_W, T_DIM);
		ty += 78;
		g_text(&g, ax + 14, ty, "SCHEDULER", T_DIM);
		snprintf(line, sizeof(line), "%s", si.policy);
		g_text(&g, ax + 110, ty, line, T_FG);
		const char *what = "fixed parameters for every task";
		if (!strcmp(si.policy, "adaptive"))
			for (unsigned i = 0; i < sizeof(class_params) / sizeof(class_params[0]); i++)
				if (!strcmp(cls, class_params[i].cls))
					what = class_params[i].what;
		g_textn(&g, ax + 110, ty + 16, what, (aw - 124) / FONT_W, T_DIM);
		ty += 42;
		g_text(&g, ax + 14, ty, "CACHE", T_DIM);
		snprintf(line, sizeof(line), "%s  (stream: %s, hits %ld.%ld%%)", cpol, iocls, hits / 10, hits % 10);
		g_textn(&g, ax + 110, ty, line, (aw - 124) / FONT_W, T_FG);
		ty += 26;
		g_text(&g, ax + 14, ty, "NETWORK", T_DIM);
		snprintf(line, sizeof(line), "%s, %u TCP connections", ni.up ? "up" : "down", ni.tcp_active);
		g_text(&g, ax + 110, ty, line, T_FG);
		g_textn(&g, ax + 110, ty + 16, "fixed policy (no adaptive network policy yet)", (aw - 124) / FONT_W, T_DIM);
		ty += 42;
		g_fill(&g, ax + 14, ty, aw - 28, 1, T_BORDER);
		ty += 10;
		if (ntrans > 0)
			snprintf(line, sizeof(line), "Last transition: %ld ms ago (%s)", ago, lastt);
		else
			snprintf(line, sizeof(line), "Last transition: none yet");
		g_textn(&g, ax + 14, ty, line, (aw - 28) / FONT_W, T_FG);
		uint64_t up = si.uptime_ns ? si.uptime_ns : 1;
		snprintf(line, sizeof(line), "Transitions since boot: %ld", ntrans);
		g_textn(&g, ax + 14, ty + 18, line, (aw - 28) / FONT_W, T_DIM);
		snprintf(line, sizeof(line), "Profiler overhead: %lu.%03lu%% of CPU time", (uint64_t)pov * 100 / up,
			 (uint64_t)pov * 100000 / up % 1000);
		g_textn(&g, ax + 14, ty + 36, line, (aw - 28) / FONT_W, T_DIM);

		/* policy controls */
		int py = ay + ah + 12;
		g_text(&g, ax, py, "Scheduler", T_DIM);
		int bw = (aw - 3 * 6) / 4;
		for (int i = 0; i < 4; i++) {
			sbtn[i] = (struct button){ ax + i * (bw + 6), py + 18, bw, 26, sched_labels[i],
						   !strcmp(si.policy, sched_pols[i]) };
			g_button(&g, &sbtn[i]);
		}
		g_text(&g, ax, py + 52, "Buffer cache", T_DIM);
		int cbw = (aw - 4 * 6) / 5;
		for (int i = 0; i < 5; i++) {
			cbtn[i] = (struct button){ ax + i * (cbw + 6), py + 70, cbw, 26, cache_pols[i],
						   !strcmp(cpol, cache_pols[i]) };
			g_button(&g, &cbtn[i]);
		}

		/* tasks + recent per-task adaptations */
		int ty2 = MAX(two ? py + 110 : py + 110, gy + 100);
		if (!two)
			ty2 = py + 110;
		g_text(&g, pad, ty2, "Tasks (class assigned by the profiler)", T_DIM);
		read_file("/proc/tasks", procbuf, sizeof(procbuf));
		char *save, *l = strtok_r(procbuf, "\n", &save);
		int row = 0, maxrows = (g.h - ty2 - 24) / 16;
		int cols = (two ? colw : W - 2 * pad) / FONT_W;
		while (l && row < maxrows) {
			g_textn(&g, pad, ty2 + 20 + row * 16, l, cols,
				row == 0 ? T_DIM : strstr(l, "INTERACTIVE") ? T_GOOD : strstr(l, "CPU_BOUND") ? T_WARN
					   : strstr(l, "IO_BOUND") ? T_ACCENT : T_FG);
			row++;
			l = strtok_r(NULL, "\n", &save);
		}
		if (two) {
			g_text(&g, ax, ty2, "Recent adaptations (per task)", T_DIM);
			char *tr = strstr(adapt, "transitions:");
			if (tr) {
				char *lines[64];
				int n = 0;
				char *s2, *t = strtok_r(strchr(tr, '\n'), "\n", &s2);
				while (t && n < 64) {
					lines[n++] = t;
					t = strtok_r(NULL, "\n", &s2);
				}
				int show = MIN(maxrows, 8);
				for (int i = MAX(0, n - show), k = 0; i < n; i++, k++)
					g_textn(&g, ax, ty2 + 20 + k * 16, lines[i], aw / FONT_W, T_DIM);
			}
		}
		gwin_present(&g);

		struct fu_wevent ev;
		uint64_t until = uptime_ns() + 1000000000ULL;
		while (uptime_ns() < until) {
			if (gwin_event(&g, &ev, 250) < 0)
				break;
			if (ev.type == WEV_CLOSE) {
				gwin_close(&g);
				return 0;
			}
			if (ev.type == WEV_RESIZE || ev.type == WEV_THEME)
				break;
			if (ev.type == WEV_MOUSE_BUTTON && ev.value == 1) {
				for (int i = 0; i < 4; i++)
					if (button_hit(&sbtn[i], ev.x, ev.y))
						sched_ctl(SCHED_SET_POLICY, 0, (char *)sched_pols[i]);
				for (int i = 0; i < 5; i++)
					if (button_hit(&cbtn[i], ev.x, ev.y))
						sys3(SYS_CACHE_CTL, CACHE_SET_POLICY, 0, cache_pols[i]);
				break; /* redraw now */
			}
		}
	}
}
