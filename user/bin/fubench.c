/* fubench - reproducible micro-benchmarks (NEW_EXPLANATION §35).
 *
 *   fubench [cpu] [mem] [storage] [net] [desktop]     (default: all but desktop)
 *
 * Every result is one line:
 *   FUBENCH {"group":"cpu","name":"prime","value":123,"unit":"ms","note":"..."}
 * A benchmark that cannot run on FuhrerOS prints "value":-1 and
 * "status":"NOT RUN" with the reason - nothing is estimated.
 * All times come from the guest's TSC clock (uptime_ns). */
#include "gui.h"

static void out(const char *group, const char *name, long value, const char *unit, const char *note)
{
	printf("FUBENCH {\"group\":\"%s\",\"name\":\"%s\",\"value\":%ld,\"unit\":\"%s\",\"note\":\"%s\"}\n", group,
	       name, value, unit, note ? note : "");
	flush();
}

static void not_run(const char *group, const char *name, const char *why)
{
	printf("FUBENCH {\"group\":\"%s\",\"name\":\"%s\",\"value\":-1,\"unit\":\"\",\"status\":\"NOT RUN\","
	       "\"note\":\"%s\"}\n",
	       group, name, why);
	flush();
}

static uint64_t ms_since(uint64_t t0) { return (uptime_ns() - t0) / 1000000; }

/* ---- CPU ---- */
static void bench_cpu(void)
{
	uint64_t t0 = uptime_ns();
	uint32_t count = 0;
	for (uint32_t n = 2; n < 300000; n++) {
		bool p = true;
		for (uint32_t d = 2; d * d <= n; d++)
			if (n % d == 0) {
				p = false;
				break;
			}
		count += p;
	}
	char note[64];
	snprintf(note, sizeof(note), "%u primes below 300000 (trial division)", count);
	out("cpu", "prime", (long)ms_since(t0), "ms", note);

	enum { N = 160 };
	static int32_t a[N][N], b[N][N], c[N][N];
	for (int i = 0; i < N; i++)
		for (int j = 0; j < N; j++) {
			a[i][j] = (i * 7 + j) % 13;
			b[i][j] = (i + j * 3) % 11;
		}
	t0 = uptime_ns();
	for (int rep = 0; rep < 4; rep++)
		for (int i = 0; i < N; i++)
			for (int j = 0; j < N; j++) {
				int32_t s = 0;
				for (int k = 0; k < N; k++)
					s += a[i][k] * b[k][j];
				c[i][j] = s + rep;
			}
	snprintf(note, sizeof(note), "4 x %dx%d int32 multiply, checksum %d", N, N, c[N - 1][N - 1]);
	out("cpu", "matrix", (long)ms_since(t0), "ms", note);
	not_run("cpu", "compilation", "no compiler has been ported to FuhrerOS");
}

/* ---- memory ---- */
static void bench_mem(void)
{
	enum { PAIRS = 100000 };
	static void *live[64];
	uint64_t rng = 88172645463325252ULL, t0 = uptime_ns();
	for (int i = 0; i < PAIRS; i++) {
		rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
		int k = (int)(rng % 64);
		free(live[k]);
		live[k] = malloc((size_t)(rng >> 32) % 4096 + 16);
	}
	uint64_t dt = uptime_ns() - t0;
	for (int k = 0; k < 64; k++)
		free(live[k]);
	out("mem", "alloc_free", (long)(dt / PAIRS), "ns/pair", "malloc 16..4111 B + free, 64 live objects");

	size_t sz = 4u << 20;
	char *src = malloc(sz), *dst = malloc(sz);
	if (!src || !dst) {
		not_run("mem", "copy", "could not allocate 2 x 4 MiB");
	} else {
		memset(src, 0x5a, sz);
		memset(dst, 0, sz);
		t0 = uptime_ns();
		for (int r = 0; r < 32; r++)
			memcpy(dst, src, sz);
		dt = uptime_ns() - t0;
		out("mem", "copy", (long)((uint64_t)32 * 4 * 1000000000ULL / (dt ? dt : 1)), "MB/s",
		    "memcpy 4 MiB x 32 (libfu memcpy, no SSE)");
	}
	free(src);
	free(dst);

	static const int kib[] = { 16, 256, 4096, 32768 };
	for (int s = 0; s < 4; s++) {
		size_t n = (size_t)kib[s] * 1024 / sizeof(uint32_t);
		uint32_t *ws = malloc(n * sizeof(uint32_t));
		char name[32];
		snprintf(name, sizeof(name), "working_set_%dK", kib[s]);
		if (!ws) {
			not_run("mem", name, "allocation failed");
			continue;
		}
		/* a single random cycle through the array (Sattolo) defeats prefetching */
		for (size_t i = 0; i < n; i++)
			ws[i] = (uint32_t)i;
		for (size_t i = n - 1; i > 0; i--) {
			rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
			size_t j = rng % i;
			uint32_t t = ws[i];
			ws[i] = ws[j];
			ws[j] = t;
		}
		uint32_t p = 0;
		enum { STEPS = 2000000 };
		t0 = uptime_ns();
		for (int i = 0; i < STEPS; i++)
			p = ws[p];
		dt = uptime_ns() - t0;
		char note[48];
		snprintf(note, sizeof(note), "dependent random loads (end %u)", p);
		out("mem", name, (long)(dt * 10 / STEPS), "ns/10 access", note);
		free(ws);
	}
}

/* ---- storage (FFS0 through the buffer cache) ---- */
static void drop_cache(void) { sys3(SYS_CACHE_CTL, CACHE_DROP, 0, 0); }

static void bench_storage(void)
{
	const char *path = "/home/.fubench.dat";
	enum { MB = 32, CH = 65536 };
	static char buf[CH];
	for (int i = 0; i < CH; i++)
		buf[i] = (char)(i * 31);
	uint64_t t0 = uptime_ns();
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
	if (fd < 0) {
		not_run("storage", "seq_write", "cannot create /home/.fubench.dat");
		return;
	}
	for (int i = 0; i < MB * 1024 * 1024 / CH; i++)
		write(fd, buf, CH);
	close(fd);
	sync();
	uint64_t dt = uptime_ns() - t0;
	out("storage", "seq_write", (long)((uint64_t)MB * 1000000000ULL * 100 / (dt ? dt : 1)), "MB/s x100",
	    "32 MiB in 64 KiB writes + sync");

	drop_cache();
	t0 = uptime_ns();
	fd = open(path, O_RDONLY);
	uint64_t total = 0;
	ssize_t r;
	while ((r = read(fd, buf, CH)) > 0)
		total += (uint64_t)r;
	close(fd);
	dt = uptime_ns() - t0;
	out("storage", "seq_read", (long)(total * 100 * 1000000000ULL / 1048576 / (dt ? dt : 1)), "MB/s x100",
	    "cold cache, 64 KiB reads");

	uint64_t blocks = (uint64_t)MB * 256, rng = 0x9E3779B97F4A7C15ULL;
	drop_cache();
	fd = open(path, O_RDONLY);
	t0 = uptime_ns();
	for (int i = 0; i < 2000; i++) {
		rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
		lseek(fd, (long)((rng % blocks) * 4096), SEEK_SET);
		read(fd, buf, 4096);
	}
	dt = uptime_ns() - t0;
	close(fd);
	out("storage", "rand_read", (long)(2000ULL * 1000000000ULL / (dt ? dt : 1)), "ops/s", "cold cache, 4 KiB");

	fd = open(path, O_RDWR);
	t0 = uptime_ns();
	for (int i = 0; i < 2000; i++) {
		rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
		lseek(fd, (long)((rng % blocks) * 4096), SEEK_SET);
		write(fd, buf, 4096);
	}
	close(fd);
	sync();
	dt = uptime_ns() - t0;
	out("storage", "rand_write", (long)(2000ULL * 1000000000ULL / (dt ? dt : 1)), "ops/s", "4 KiB + final sync");
	unlink(path);

	mkdir("/home/.fubench.d");
	enum { NF = 500 };
	char fp[64];
	t0 = uptime_ns();
	for (int i = 0; i < NF; i++) {
		snprintf(fp, sizeof(fp), "/home/.fubench.d/f%03d", i);
		int f = open(fp, O_WRONLY | O_CREAT | O_TRUNC);
		write(f, buf, 4096);
		close(f);
	}
	sync();
	dt = uptime_ns() - t0;
	out("storage", "small_file_create", (long)((uint64_t)NF * 1000000000ULL / (dt ? dt : 1)), "files/s",
	    "500 x 4 KiB create+write+close, then sync");
	t0 = uptime_ns();
	for (int i = 0; i < NF; i++) {
		snprintf(fp, sizeof(fp), "/home/.fubench.d/f%03d", i);
		unlink(fp);
	}
	sync();
	dt = uptime_ns() - t0;
	unlink("/home/.fubench.d");
	out("storage", "small_file_delete", (long)((uint64_t)NF * 1000000000ULL / (dt ? dt : 1)), "files/s",
	    "500 unlinks, then sync");
}

/* ---- network (own TCP/IP stack; traffic to the guest's own address) ---- */
static volatile uint64_t sink_bytes;
static volatile int sink_ready, echo_ready;

static void tcp_sink(void *arg)
{
	int s = socket(AF_INET, SOCK_STREAM);
	bind(s, 7101);
	listen(s, 1);
	sink_ready = 1;
	int c = accept(s, NULL);
	static char b[65536];
	ssize_t r;
	while ((r = recv(c, b, sizeof(b))) > 0)
		sink_bytes += (uint64_t)r;
	close(c);
	close(s);
}

static void tcp_echo(void *arg)
{
	int s = socket(AF_INET, SOCK_STREAM);
	bind(s, 7102);
	listen(s, 1);
	echo_ready = 1;
	int c = accept(s, NULL);
	char b[64];
	ssize_t r;
	while ((r = recv(c, b, sizeof(b))) > 0)
		send(c, b, (size_t)r);
	close(c);
	close(s);
}

static volatile uint64_t udp_got;
static volatile int udp_ready;
static void udp_sink(void *arg)
{
	int s = socket(AF_INET, SOCK_DGRAM);
	bind(s, 7103);
	udp_ready = 1;
	char b[128];
	while (recvfrom(s, b, sizeof(b), NULL, 500) > 0)
		udp_got++;
	close(s);
}

static void bench_net(void)
{
	struct fu_netinfo ni;
	netinfo(&ni);
	if (!ni.up) {
		not_run("net", "tcp_throughput", "network interface down");
		return;
	}
	thread_create(tcp_sink, NULL, 0);
	while (!sink_ready)
		sleep_ms(1);
	int s = socket(AF_INET, SOCK_STREAM);
	if (connect(s, ni.ip, 7101) < 0) {
		not_run("net", "tcp_throughput", "connect failed");
	} else {
		static char b[16384];
		memset(b, 7, sizeof(b));
		uint64_t t0 = uptime_ns(), sent = 0;
		while (sent < (32u << 20)) {
			ssize_t w = send(s, b, sizeof(b));
			if (w <= 0)
				break;
			sent += (uint64_t)w;
		}
		close(s);
		for (int i = 0; i < 200 && sink_bytes < sent; i++)
			sleep_ms(5);
		uint64_t dt = uptime_ns() - t0;
		out("net", "tcp_throughput", (long)(sink_bytes * 100 * 1000000000ULL / 1048576 / (dt ? dt : 1)),
		    "MB/s x100", "32 MiB to the guest's own IP (loopback path of the own stack)");
	}

	thread_create(tcp_echo, NULL, 0);
	while (!echo_ready)
		sleep_ms(1);
	s = socket(AF_INET, SOCK_STREAM);
	if (connect(s, ni.ip, 7102) < 0) {
		not_run("net", "tcp_rtt", "connect failed");
	} else {
		enum { RT = 2000 };
		static uint64_t rtt[RT];
		char c = 'x';
		int n = 0;
		for (int i = 0; i < RT; i++) {
			uint64_t t0 = uptime_ns();
			if (send(s, &c, 1) != 1 || recv(s, &c, 1) != 1)
				break;
			rtt[n++] = uptime_ns() - t0;
		}
		close(s);
		qsort_u64(rtt, n);
		char note[64];
		snprintf(note, sizeof(note), "1-byte ping-pong x %d, p99 %lu us", n,
			 n ? rtt[(n - 1) * 99 / 100] / 1000 : 0);
		out("net", "tcp_rtt_p50", n ? (long)(rtt[n / 2] / 1000) : -1, "us", note);
	}

	thread_create(udp_sink, NULL, 0);
	while (!udp_ready)
		sleep_ms(1);
	int u = socket(AF_INET, SOCK_DGRAM);
	char pkt[64] = "fubench";
	/* UDP has no flow control: an unpaced sender only measures how fast
	 * datagrams can be dropped (a first version reported 20000 sent, 32
	 * received). Keep at most 16 in flight and report the delivered rate. */
	enum { NP = 20000 };
	uint64_t t0 = uptime_ns();
	int sent = 0;
	for (int i = 0; i < NP; i++) {
		for (int spin = 0; (uint64_t)sent - udp_got >= 16 && spin < 100000; spin++)
			yield();
		sent += sendto(u, pkt, sizeof(pkt), ni.ip, 7103) > 0;
	}
	for (int i = 0; i < 100 && udp_got < (uint64_t)sent; i++)
		sleep_ms(1);
	uint64_t dt = uptime_ns() - t0, got = udp_got;
	close(u);
	char note[96];
	snprintf(note, sizeof(note), "64 B datagrams, <=16 in flight: %d sent, %lu delivered", sent, got);
	out("net", "udp_delivered_rate", (long)(got * 1000000000ULL / (dt ? dt : 1)), "packets/s", note);
}

/* ---- desktop ---- */
static char dbuf[8192];

static bool window_of(pid_t pid)
{
	char key[24];
	snprintf(key, sizeof(key), " pid %d ", pid);
	read_file("/proc/desktop", dbuf, sizeof(dbuf));
	return strstr(dbuf, key) != NULL;
}

static void bench_desktop(void)
{
	if (sys2(SYS_DESKTOP_CTL, DESK_START, 0) < 0) {
		not_run("desktop", "window_create", "no framebuffer");
		return;
	}
	sleep_ms(300);
	/* window creation: create + first present + close */
	uint64_t total = 0;
	for (int i = 0; i < 20; i++) {
		struct gwin g;
		uint64_t t0 = uptime_ns();
		if (gwin_open(&g, 400, 300, "fubench") < 0) {
			not_run("desktop", "window_create", "gwin_open failed");
			return;
		}
		g_fill(&g, 0, 0, g.w, g.h, T_BG);
		gwin_present(&g);
		total += uptime_ns() - t0;
		gwin_close(&g);
	}
	out("desktop", "window_create", (long)(total / 20 / 1000), "us", "open 400x300 + fill + present, avg of 20");

	/* application launch: spawn -> the app's window exists */
	static const struct {
		const char *name, *path;
	} apps[] = { { "launch_terminal", "/bin/term" }, { "launch_files", "/bin/files" },
		     { "launch_editor", "/bin/edit" }, { "launch_browser", "/bin/web" },
		     { "launch_settings", "/bin/settings" }, { "launch_control_center", "/bin/control" } };
	for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
		uint64_t best = ~0ULL, sum = 0;
		int ok = 0;
		for (int rep = 0; rep < 3; rep++) {
			const char *argv[] = { apps[i].path, NULL };
			uint64_t t0 = uptime_ns();
			pid_t p = spawn(apps[i].path, argv, NULL);
			if (p < 0)
				break;
			while (!window_of(p) && ms_since(t0) < 5000)
				sleep_ms(1);
			uint64_t dt = uptime_ns() - t0;
			bool seen = window_of(p);
			kill(p);
			wait(p, NULL);
			if (!seen)
				continue;
			ok++;
			sum += dt;
			best = MIN(best, dt);
			sleep_ms(100);
		}
		char note[64];
		snprintf(note, sizeof(note), "spawn -> window exists; mean of %d, best %lu ms", ok,
			 ok ? best / 1000000 : 0);
		if (ok)
			out("desktop", apps[i].name, (long)(sum / (uint64_t)ok / 1000000), "ms", note);
		else
			not_run("desktop", apps[i].name, "window did not appear within 5 s");
	}

	/* workspace switching: request -> frame on screen (measured by the compositor) */
	uint64_t ws_sum = 0;
	int ws_n = 0;
	for (int i = 0; i < 10; i++) {
		sys2(SYS_DESKTOP_CTL, DESK_WORKSPACE, (i + 1) % 2);
		sleep_ms(80);
		read_file("/proc/desktop", dbuf, sizeof(dbuf));
		long us = text_num(dbuf, "last_workspace_switch_us");
		if (us > 0) {
			ws_sum += (uint64_t)us;
			ws_n++;
		}
	}
	sys2(SYS_DESKTOP_CTL, DESK_WORKSPACE, 0);
	if (ws_n)
		out("desktop", "workspace_switch", (long)(ws_sum / (uint64_t)ws_n), "us",
		    "switch request -> composed frame, avg of 10");
	else
		not_run("desktop", "workspace_switch", "no measurement from the compositor");
	read_file("/proc/desktop", dbuf, sizeof(dbuf));
	out("desktop", "compose_avg", text_num(dbuf, "avg_compose_us"), "us", "average full composition, whole run");
	out("desktop", "compose_max", text_num(dbuf, "max_compose_us"), "us", "worst composition, whole run");
	not_run("desktop", "terminal_keystroke_latency",
		"needs injected keyboard input; not measurable from inside the guest yet");
}

int main(int argc, char **argv)
{
	bool all = argc < 2;
	for (int i = 0; i < argc || all; i++) {
		const char *w = all ? NULL : argv[i];
		if (all || !strcmp(w, "cpu"))
			bench_cpu();
		if (all || !strcmp(w, "mem"))
			bench_mem();
		if (all || !strcmp(w, "storage"))
			bench_storage();
		if (all || !strcmp(w, "net"))
			bench_net();
		if (!all && !strcmp(w, "desktop"))
			bench_desktop();
		if (all)
			break;
	}
	return 0;
}
