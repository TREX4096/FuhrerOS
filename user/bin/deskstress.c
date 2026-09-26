/* deskstress - desktop-style mixed workload (NEW_EXPLANATION M17 and §35
 * "mixed workload", UI_SUGGESTION §41).
 *
 *   deskstress [-p POLICY] [-t SECONDS] [-n NAME]
 *
 * Runs, at the same time, for SECONDS:
 *   browser   : HTTP GETs of /www/index.html from a local httpd (own TCP stack)
 *   build     : two CPU-bound workers (a stand-in for compilation: no compiler
 *               has been ported, so this is labelled as a proxy, not a build)
 *   copy      : copies an 8 MiB file on FFS0 over and over
 *   network   : a TCP stream to a draining thread
 *   terminal  : an interactive probe (sleep 10 ms, ~0.2 ms of work) whose
 *               dispatch / wake-up latency stands for terminal responsiveness
 * "media" is NOT RUN: FuhrerOS has no audio or video path.
 * Output: DESKSTRESS {json} with per-component throughput and probe latency. */
#include "fu.h"

static volatile bool stop;
static volatile uint64_t build_jobs, copy_bytes, web_reqs, web_fail, net_bytes;
static volatile int ready;
#define MAXS 8192
static uint64_t wake[MAXS], disp[MAXS];
static volatile int ns;
static uint32_t ip;

static void probe(void *a)
{
	while (!stop) {
		uint64_t want = uptime_ns() + 10000000UL;
		sleep_ms(10);
		uint64_t woke = uptime_ns();
		long d = sys3(SYS_SCHED_CTL, SCHED_LAST_DISPATCH, 0, 0);
		volatile uint32_t x = 0;
		for (int i = 0; i < 20000; i++)
			x += (uint32_t)i * 2654435761u;
		int k = ns;
		if (k < MAXS) {
			wake[k] = woke > want ? woke - want : 0;
			disp[k] = d > 0 ? (uint64_t)d : 0;
			ns = k + 1;
		}
	}
	ready--;
}

static void build(void *a)
{
	while (!stop) {
		uint32_t c = 0;
		for (uint32_t n = 2; n < 20000; n++) {
			bool p = true;
			for (uint32_t d = 2; d * d <= n; d++)
				if (n % d == 0) {
					p = false;
					break;
				}
			c += p;
		}
		if (c)
			build_jobs++;
	}
	ready--;
}

static void copy(void *a)
{
	static char buf[65536];
	while (!stop) {
		int in = open("/home/.stress_src", O_RDONLY);
		int out = open("/home/.stress_dst", O_WRONLY | O_CREAT | O_TRUNC);
		ssize_t r;
		while (!stop && (r = read(in, buf, sizeof(buf))) > 0) {
			write(out, buf, (size_t)r);
			copy_bytes += (uint64_t)r;
		}
		close(in);
		close(out);
	}
	ready--;
}

static void web(void *a)
{
	static char buf[4096];
	while (!stop) {
		int s = socket(AF_INET, SOCK_STREAM);
		if (connect(s, ip, 8081) < 0) {
			web_fail++;
			close(s);
			sleep_ms(20);
			continue;
		}
		const char *req = "GET /index.html HTTP/1.0\r\n\r\n";
		send(s, req, strlen(req));
		uint64_t got = 0;
		ssize_t r;
		while ((r = recv(s, buf, sizeof(buf))) > 0)
			got += (uint64_t)r;
		close(s);
		if (got)
			web_reqs++;
		else
			web_fail++;
	}
	ready--;
}

static void net_sink(void *a)
{
	int s = socket(AF_INET, SOCK_STREAM);
	bind(s, 7201);
	listen(s, 1);
	ready++;
	int c = accept(s, NULL);
	static char b[65536];
	ssize_t r;
	while ((r = recv(c, b, sizeof(b))) > 0)
		net_bytes += (uint64_t)r;
	close(c);
	close(s);
}

static void net_src(void *a)
{
	static char b[16384];
	int s = socket(AF_INET, SOCK_STREAM);
	if (connect(s, ip, 7201) == 0)
		while (!stop && send(s, b, sizeof(b)) > 0)
			;
	close(s);
	ready--;
}

static uint64_t pct(uint64_t *v, int n, int p) { return n ? v[(long)(n - 1) * p / 100] : 0; }

int main(int argc, char **argv)
{
	int secs = 10;
	const char *name = "stress";
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "-p"))
			sched_ctl(SCHED_SET_POLICY, 0, argv[i + 1]);
		else if (!strcmp(argv[i], "-t"))
			secs = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-n"))
			name = argv[i + 1];
	}
	char pol[32] = "";
	sched_ctl(SCHED_GET_POLICY, 0, pol);
	struct fu_netinfo ni;
	netinfo(&ni);
	ip = ni.ip;
	/* 8 MiB source file for the copy workload */
	struct stat st;
	if (stat("/home/.stress_src", &st) < 0 || st.size != (8u << 20)) {
		static char b[65536];
		memset(b, 3, sizeof(b));
		int f = open("/home/.stress_src", O_WRONLY | O_CREAT | O_TRUNC);
		for (int i = 0; i < 128; i++)
			write(f, b, sizeof(b));
		close(f);
		sync();
	}
	const char *hargv[] = { "/bin/httpd", "8081", "/www", NULL };
	pid_t httpd = spawn("/bin/httpd", hargv, NULL);
	sleep_ms(300);

	struct fu_sysinfo s0, s1;
	thread_create(net_sink, NULL, 0);
	while (ready < 1)
		sleep_ms(1);
	ready = 0;
	sysinfo(&s0);
	uint64_t t0 = uptime_ns();
	ready = 6;
	thread_create(probe, NULL, 0);
	thread_create(build, NULL, 0);
	thread_create(build, NULL, 0);
	thread_create(copy, NULL, 0);
	thread_create(web, NULL, 0);
	thread_create(net_src, NULL, 0);
	sleep_ms((uint64_t)secs * 1000 / 2);
	char cls[24] = "";
	static char ad[8192];
	read_file("/proc/adapt", ad, sizeof(ad));
	text_value(ad, "system_class", cls, sizeof(cls));
	sleep_ms((uint64_t)secs * 1000 - (uint64_t)secs * 1000 / 2);
	stop = true;
	uint64_t el = uptime_ns() - t0;
	for (int i = 0; i < 400 && ready > 0; i++)
		sleep_ms(10);
	sysinfo(&s1);
	if (httpd > 0) {
		kill(httpd);
		wait(httpd, NULL);
	}
	unlink("/home/.stress_dst");
	int n = ns;
	qsort_u64(wake, n);
	qsort_u64(disp, n);
	uint64_t cs = (s1.context_switches - s0.context_switches) * 1000000000ULL / el;
	printf("DESKSTRESS {\"name\":\"%s\",\"policy\":\"%s\",\"seconds\":%d,\"samples\":%d,"
	       "\"dispatch_p50_us\":%lu,\"dispatch_p99_us\":%lu,\"wake_p50_us\":%lu,\"wake_p99_us\":%lu,"
	       "\"build_jobs_per_s_x100\":%lu,\"copy_mb_per_s_x100\":%lu,\"web_req_per_s_x100\":%lu,"
	       "\"web_failures\":%lu,\"net_mb_per_s_x100\":%lu,\"ctx_switches_per_s\":%lu,\"system_class\":\"%s\","
	       "\"media\":\"NOT RUN\"}\n",
	       name, pol, secs, n, pct(disp, n, 50) / 1000, pct(disp, n, 99) / 1000, pct(wake, n, 50) / 1000,
	       pct(wake, n, 99) / 1000, build_jobs * 100000000000UL / el,
	       copy_bytes / 1024 * 100000000000UL / el / 1024, web_reqs * 100000000000UL / el, web_fail,
	       net_bytes / 1024 * 100000000000UL / el / 1024, cs, cls);
	flush();
	return 0;
}
