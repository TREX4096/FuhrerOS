/* control — Fuhrer Control Center (NEW_EXPLANATION §33).
 * Shows the adaptive kernel live: resource bars, CPU history, the detected
 * system workload with confidence and reason, per-task classes, the
 * scheduling and cache policies (switchable), and recent adaptations. */
#include "gui.h"

#define HIST 60
static int cpu_hist[HIST];
static char procbuf[32768];

static const char *sched_pols[] = { "round_robin", "priority", "low_latency", "adaptive" };
static const char *sched_labels[] = { "Round robin", "Priority", "Low latency", "Adaptive" };
static const char *cache_pols[] = { "lru", "fifo", "clock", "readahead", "adaptive" };

static struct button sbtn[4], cbtn[5];

static void section(struct gwin *g, int x, int y, const char *t)
{
	g_text(g, x, y, t, T_DIM);
	g_fill(g, x, y + 18, 400, 1, RGB(0x2e, 0x36, 0x48));
}

int main(void)
{
	struct gwin g;
	if (gwin_open(&g, 860, 600, "Fuhrer Control Center") < 0)
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
		uint64_t diskb = (bs.read_bytes + bs.write_bytes) - (bprev.read_bytes + bprev.write_bytes);
		uint64_t netb = (ni.rx_bytes + ni.tx_bytes) - (nprev.rx_bytes + nprev.tx_bytes);
		uint64_t disk_kbs = dt ? diskb * 1000000000ULL / dt / 1024 : 0;
		uint64_t net_kbs = dt ? netb * 1000000000ULL / dt / 1024 : 0;
		uint64_t cs = dt ? (si.context_switches - prev.context_switches) * 1000000000ULL / dt : 0;
		memmove(cpu_hist, cpu_hist + 1, sizeof(int) * (HIST - 1));
		cpu_hist[HIST - 1] = cpu;
		prev = si;
		nprev = ni;
		bprev = bs;
		tprev = now;

		g_fill(&g, 0, 0, g.w, g.h, T_BG);
		g_text_big(&g, 20, 14, "Fuhrer Control Center", T_FG, 2);
		char line[160];
		snprintf(line, sizeof(line), "FuhrerOS kernel  |  %s  |  up %lus", si.cpu_brand,
			 si.uptime_ns / 1000000000UL);
		g_text(&g, 20, 52, line, T_DIM);

		int x = 20, y = 84;
		section(&g, x, y, "Resources");
		struct { const char *n; int pct; const char *extra; } bars[4];
		char e0[32], e1[32], e2[32], e3[32];
		snprintf(e0, sizeof(e0), "%d%%  %lu cs/s", cpu, cs);
		snprintf(e1, sizeof(e1), "%lu / %lu MiB", (si.mem_total_kb - si.mem_free_kb) / 1024, si.mem_total_kb / 1024);
		snprintf(e2, sizeof(e2), "%lu KiB/s", disk_kbs);
		snprintf(e3, sizeof(e3), "%lu KiB/s", net_kbs);
		bars[0].n = "CPU"; bars[0].pct = cpu; bars[0].extra = e0;
		bars[1].n = "Memory"; bars[1].pct = mem; bars[1].extra = e1;
		bars[2].n = "Disk"; bars[2].pct = (int)MIN(disk_kbs / 200, 100); bars[2].extra = e2;
		bars[3].n = "Network"; bars[3].pct = (int)MIN(net_kbs / 100, 100); bars[3].extra = e3;
		for (int i = 0; i < 4; i++) {
			int by = y + 28 + i * 28;
			g_text(&g, x, by, bars[i].n, T_FG);
			g_bar(&g, x + 80, by + 2, 200, 12, bars[i].pct, i == 0 ? T_ACCENT : i == 1 ? T_GOOD : i == 2 ? T_WARN : RGB(0xc6, 0x78, 0xdd));
			g_text(&g, x + 292, by, bars[i].extra, T_DIM);
		}
		g_graph(&g, x, y + 146, 400, 70, cpu_hist, HIST, T_ACCENT);
		g_text(&g, x + 4, y + 150, "CPU (60 s)", T_DIM);

		/* adaptive state */
		int x2 = 450;
		section(&g, x2, y, "Adaptive kernel");
		static char adapt[16384];
		read_file("/proc/adapt", adapt, sizeof(adapt));
		char cls[32], reason[96], conf[16];
		text_value(adapt, "system_class", cls, sizeof(cls));
		text_value(adapt, "reason", reason, sizeof(reason));
		text_value(adapt, "confidence", conf, sizeof(conf));
		g_text(&g, x2, y + 28, "Current workload", T_DIM);
		g_text_big(&g, x2, y + 46, cls, T_ACCENT, 2);
		snprintf(line, sizeof(line), "Confidence %s%%  -  %s", conf, reason);
		g_text(&g, x2, y + 84, line, T_FG);
		static char bc[4096];
		read_file("/proc/bcache", bc, sizeof(bc));
		char cpol[24], iocls[24];
		text_value(bc, "policy", cpol, sizeof(cpol));
		text_value(bc, "io_class", iocls, sizeof(iocls));
		long hits = text_num(bc, "hit_rate_pm");
		snprintf(line, sizeof(line), "Scheduling policy: %s", si.policy);
		g_text(&g, x2, y + 110, line, T_FG);
		snprintf(line, sizeof(line), "Cache policy: %s (I/O: %s, hit rate %ld.%ld%%)", cpol, iocls, hits / 10, hits % 10);
		g_text(&g, x2, y + 128, line, T_FG);
		snprintf(line, sizeof(line), "Network: %s, %u TCP connections", ni.up ? "up" : "down", ni.tcp_active);
		g_text(&g, x2, y + 146, line, T_FG);

		g_text(&g, x2, y + 172, "Scheduler", T_DIM);
		for (int i = 0; i < 4; i++) {
			sbtn[i] = (struct button){ x2 + i * 98, y + 190, 94, 26, sched_labels[i], !strcmp(si.policy, sched_pols[i]) };
			g_button(&g, &sbtn[i]);
		}
		g_text(&g, x2, y + 224, "Buffer cache", T_DIM);
		for (int i = 0; i < 5; i++) {
			cbtn[i] = (struct button){ x2 + i * 78, y + 242, 74, 26, cache_pols[i], !strcmp(cpol, cache_pols[i]) };
			g_button(&g, &cbtn[i]);
		}

		/* tasks */
		int ty = 380;
		section(&g, 20, ty, "Tasks (class assigned by the profiler)");
		read_file("/proc/tasks", procbuf, sizeof(procbuf));
		char *save, *l = strtok_r(procbuf, "\n", &save);
		int row = 0;
		while (l && row < 11) {
			if (row == 0)
				g_text(&g, 20, ty + 24, l, T_DIM);
			else
				g_text(&g, 20, ty + 24 + row * 16, l, strstr(l, "INTERACTIVE") ? T_GOOD : strstr(l, "CPU_BOUND") ? T_WARN : strstr(l, "IO_BOUND") ? T_ACCENT : T_FG);
			row++;
			l = strtok_r(NULL, "\n", &save);
		}
		/* recent adaptations */
		section(&g, 450, 300, "Recent adaptations");
		char *tr = strstr(adapt, "transitions:");
		int k = 0;
		if (tr) {
			char *lines[64];
			int n = 0;
			char *s2, *t = strtok_r(strchr(tr, '\n'), "\n", &s2);
			while (t && n < 64) {
				lines[n++] = t;
				t = strtok_r(NULL, "\n", &s2);
			}
			for (int i = MAX(0, n - 4); i < n; i++, k++)
				g_textn(&g, 450, 324 + k * 16, lines[i], 50, T_DIM);
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
