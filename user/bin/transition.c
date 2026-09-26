/* transition - the workload-transition experiment (NEW_EXPLANATION §36).
 *
 *   transition [-s SECONDS_PER_PHASE] [-p SCHED_POLICY]
 * Phases: cpu -> random-io -> sequential-io -> network -> interactive.
 * Every 250 ms it records the phase, the kernel's detected system class
 * (with confidence), the scheduling and cache policies in force, the cache's
 * I/O class, and the phase's throughput, as lines:
 *   TL {"t_ms":..,"phase":..,"class":..,"conf":..,"sched":..,"cache":..,"io_class":..,"ops":..}
 * and finally TRANSITION {summary} with per-phase detection delay. */
#include "fu.h"

static volatile int phase = -1;
static volatile bool quit;
static volatile uint64_t ops;
static bool debug;
static const char *phases[] = { "cpu", "random-io", "sequential-io", "network", "interactive" };
static const char *expect[] = { "CPU_BOUND", "IO_BOUND", "IO_BOUND", "IO_BOUND", "INTERACTIVE" };

static void worker(void *arg)
{
	int id = (int)(uintptr_t)arg;
	static char buf[65536];
	int fd = -1, sock = -1;
	int my_phase = -2;
	uint64_t blocks = 0, seqpos = (uint64_t)id * 1000;
	while (!quit) {
		if (phase != my_phase) {
			my_phase = phase;
			if (fd >= 0) { close(fd); fd = -1; }
			if (sock >= 0) { close(sock); sock = -1; }
			if (my_phase == 1 || my_phase == 2) {
				fd = open("/home/.iobench.dat", O_RDONLY);
				struct stat st;
				fstat(fd, &st);
				blocks = st.size / 4096;
			} else if (my_phase == 3) {
				struct fu_netinfo ni;
				netinfo(&ni);
				sock = socket(AF_INET, SOCK_STREAM);
				if (connect(sock, ni.ip, 7070) < 0) {
					close(sock);
					sock = -1;
				}
			}
		}
		switch (my_phase) {
		case 0: {
			volatile uint64_t x = 0;
			for (int i = 0; i < 200000; i++)
				x += (uint64_t)i * i;
			ops++;
			break;
		}
		case 1:
			if (fd >= 0 && blocks) {
				lseek(fd, (long)((rand32() % blocks) * 4096), SEEK_SET);
				read(fd, buf, 4096);
				ops++;
			}
			break;
		case 2:
			if (fd >= 0 && blocks) {
				lseek(fd, (long)((seqpos++ % blocks) * 4096), SEEK_SET);
				read(fd, buf, 4096);
				ops++;
			}
			break;
		case 3:
			if (sock >= 0 && send(sock, buf, 16384) > 0)
				ops++;
			else
				sleep_ms(5);
			break;
		case 4:
			sleep_ms(8);
			{
				volatile int x = 0;
				for (int i = 0; i < 5000; i++)
					x += i;
			}
			ops++;
			break;
		default:
			sleep_ms(5);
		}
	}
}

static void drain(void *arg)
{
	int c = (int)(uintptr_t)arg;
	static char buf[2][65536];
	static volatile int slot;
	char *b = buf[__atomic_fetch_add(&slot, 1, __ATOMIC_RELAXED) & 1];
	ssize_t r;
	uint64_t total = 0;
	while ((r = recv(c, b, 65536)) > 0)
		total += (uint64_t)r;
	if (debug)
		printf("DEBUG drain fd %d exits: recv=%ld after %lu bytes\n", c, (long)r, total);
	close(c);
}

/* One draining thread per connection (both workers connect). */
static void sink_server(void *arg)
{
	int s = socket(AF_INET, SOCK_STREAM);
	bind(s, 7070);
	listen(s, 4);
	for (;;) {
		int c = accept(s, NULL);
		if (c >= 0)
			thread_create(drain, (void *)(uintptr_t)c, 0);
	}
}

int main(int argc, char **argv)
{
	int secs = 10;
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-d"))
			debug = true;
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "-s")) secs = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-p")) sched_ctl(SCHED_SET_POLICY, 0, argv[i + 1]);
	}
	struct stat st;
	if (stat("/home/.iobench.dat", &st) < 0) {
		const char *a[] = { "/bin/iobench", "-t", "1", NULL };
		pid_t p = spawn("/bin/iobench", a, NULL);
		wait(p, NULL);
	}
	sys3(SYS_CACHE_CTL, CACHE_SET_CAPACITY, 1024, 0);
	sys3(SYS_CACHE_CTL, CACHE_DROP, 0, 0);
	thread_create(sink_server, NULL, 0);
	for (int i = 0; i < 2; i++)
		thread_create(worker, (void *)(uintptr_t)i, 0);
	char pol[32] = "";
	sched_ctl(SCHED_GET_POLICY, 0, pol);
	uint64_t t0 = uptime_ns();
	int64_t detect_ms[5];
	for (int p = 0; p < 5; p++) {
		detect_ms[p] = -1;
		phase = p;
		uint64_t pstart = uptime_ns();
		uint64_t last_ops = ops;
		while (uptime_ns() - pstart < (uint64_t)secs * 1000000000ULL) {
			sleep_ms(250);
			static char a[8192], b[4096];
			read_file("/proc/adapt", a, sizeof(a));
			read_file("/proc/bcache", b, sizeof(b));
			char cls[24], conf[8], cpol[24], iocls[24];
			text_value(a, "system_class", cls, sizeof(cls));
			text_value(a, "confidence", conf, sizeof(conf));
			text_value(b, "policy", cpol, sizeof(cpol));
			text_value(b, "io_class", iocls, sizeof(iocls));
			uint64_t o = ops;
			printf("TL {\"t_ms\":%lu,\"phase\":\"%s\",\"class\":\"%s\",\"conf\":%s,\"sched\":\"%s\",\"cache\":\"%s\",\"io_class\":\"%s\",\"ops\":%lu}\n",
			       (uptime_ns() - t0) / 1000000, phases[p], cls, conf[0] ? conf : "0", pol, cpol, iocls,
			       o - last_ops);
			last_ops = o;
			if (detect_ms[p] < 0 && !strcmp(cls, expect[p]))
				detect_ms[p] = (int64_t)((uptime_ns() - pstart) / 1000000);
		}
		if (debug) {
			static char dbg[16384];
			read_file("/proc/net", dbg, sizeof(dbg));
			printf("DEBUG phase %s /proc/net:\n%s", phases[p], dbg);
			read_file("/proc/tasks", dbg, sizeof(dbg));
			printf("DEBUG /proc/tasks:\n%s", dbg);
		}
	}
	quit = true;
	printf("TRANSITION {\"policy\":\"%s\",\"phase_seconds\":%d", pol, secs);
	for (int p = 0; p < 5; p++)
		printf(",\"detect_ms_%s\":%ld", phases[p], (long)detect_ms[p]);
	printf("}\n");
	flush();
	return 0;
}
