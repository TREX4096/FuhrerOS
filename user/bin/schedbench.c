/* schedbench - scheduler evaluation workload (NEW_EXPLANATION §17, §35, M16).
 *
 *   schedbench [-p POLICY] [-t SECONDS] [-c CPU_WORKERS] [-i IO_WORKERS] [-n NAME]
 *
 * Runs, concurrently:
 *   - an interactive probe: sleeps 10 ms, then does ~0.2 ms of work, and
 *     measures wake-up latency (actual wake - requested wake) and response
 *     time (wake request -> work finished);
 *   - CPU-bound workers: fixed-size compute jobs (prime sieve), counted;
 *   - I/O-bound workers: random 4 KiB reads of a file through the buffer
 *     cache (small reads, frequent blocking).
 * Reports P50/P95/P99 latency, batch throughput, Jain fairness across CPU
 * workers, context switches/s and scheduler overhead, as one line:
 *   SCHEDBENCH {json}
 * All numbers are measured in the guest with the TSC-based clock. */
#include "fu.h"

#define MAX_SAMPLES 20000
#define MAXW 8

static volatile bool stop;
static volatile uint64_t cpu_jobs[MAXW];
static volatile uint64_t io_ops[MAXW];
static uint64_t wake_lat[MAX_SAMPLES], resp[MAX_SAMPLES];
static volatile int nsamples;
static volatile int done_threads;

static uint32_t sieve(uint32_t n)
{
	static uint8_t marks[MAXW][20001];
	(void)marks;
	uint32_t count = 0;
	for (uint32_t i = 2; i < n; i++) {
		bool prime = true;
		for (uint32_t d = 2; d * d <= i; d++)
			if (i % d == 0) {
				prime = false;
				break;
			}
		count += prime;
	}
	return count;
}

static void cpu_worker(void *arg)
{
	int id = (int)(uintptr_t)arg;
	volatile uint32_t sink = 0;
	while (!stop) {
		sink += sieve(3000);
		cpu_jobs[id]++;
	}
	(void)sink;
	done_threads++;
}

static void io_worker(void *arg)
{
	int id = (int)(uintptr_t)arg;
	int fd = open("/home/.schedbench.dat", O_RDONLY);
	struct stat st;
	fstat(fd, &st);
	uint64_t blocks = st.size / 4096;
	char buf[4096];
	while (!stop && fd >= 0 && blocks) {
		lseek(fd, (long)((rand32() % blocks) * 4096), SEEK_SET);
		read(fd, buf, sizeof(buf));
		io_ops[id]++;
		sleep_ms(1); /* think time: an I/O-bound task blocks often */
	}
	if (fd >= 0)
		close(fd);
	done_threads++;
}

static void probe(void *arg)
{
	while (!stop) {
		uint64_t want = uptime_ns() + 10000000UL;
		sleep_ms(10);
		uint64_t woke = uptime_ns();
		volatile uint32_t x = 0;
		for (int i = 0; i < 20000; i++) /* ~0.1-0.3 ms of work */
			x += (uint32_t)i * 2654435761u;
		uint64_t fin = uptime_ns();
		int k = nsamples;
		if (k < MAX_SAMPLES) {
			wake_lat[k] = woke > want ? woke - want : 0;
			resp[k] = fin - (want - 10000000UL);
			nsamples = k + 1;
		}
	}
	done_threads++;
}

static uint64_t pct(uint64_t *a, int n, int p)
{
	if (!n)
		return 0;
	int i = (int)((long)(n - 1) * p / 100);
	return a[i];
}

static long proc_num(const char *file, const char *key)
{
	static char buf[4096];
	read_file(file, buf, sizeof(buf));
	return text_num(buf, key);
}

int main(int argc, char **argv)
{
	const char *policy = NULL, *name = "run";
	int secs = 10, ncpu = 3, nio = 1;
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "-p")) policy = argv[i + 1];
		else if (!strcmp(argv[i], "-t")) secs = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-c")) ncpu = MIN(atoi(argv[i + 1]), MAXW);
		else if (!strcmp(argv[i], "-i")) nio = MIN(atoi(argv[i + 1]), MAXW);
		else if (!strcmp(argv[i], "-n")) name = argv[i + 1];
	}
	if (policy && sched_ctl(SCHED_SET_POLICY, 0, (char *)policy) < 0) {
		dprintf(2, "schedbench: unknown policy %s\n", policy);
		return 2;
	}
	char pol[32] = "";
	sched_ctl(SCHED_GET_POLICY, 0, pol);
	/* data file for the I/O workers (16 MiB, created once) */
	struct stat st;
	if (nio && (stat("/home/.schedbench.dat", &st) < 0 || st.size < (16 << 20))) {
		int fd = open("/home/.schedbench.dat", O_WRONLY | O_CREAT | O_TRUNC);
		static char blk[65536];
		for (int i = 0; i < 256; i++)
			write(fd, blk, sizeof(blk));
		close(fd);
		sync();
	}
	struct fu_sysinfo s0, s1;
	long ov0 = proc_num("/proc/sched", "sched_overhead_ns");
	sysinfo(&s0);
	uint64_t t0 = uptime_ns();
	stop = false;
	int tids[MAXW * 2 + 1], kinds[MAXW * 2 + 1], nt = 0; /* kind 0 cpu, 1 io, 2 probe */
	for (int i = 0; i < ncpu; i++) {
		kinds[nt] = 0;
		tids[nt++] = thread_create(cpu_worker, (void *)(uintptr_t)i, 0);
	}
	for (int i = 0; i < nio; i++) {
		kinds[nt] = 1;
		tids[nt++] = thread_create(io_worker, (void *)(uintptr_t)i, 0);
	}
	kinds[nt] = 2;
	tids[nt++] = thread_create(probe, NULL, 0);
	/* mid-run: which class did the kernel assign to each worker, and what
	 * does it think the whole system is doing? (evidence for RQ1) */
	sleep_ms((uint64_t)secs * 500);
	static const char *cn[] = { "UNKNOWN", "IDLE", "INTERACTIVE", "IO_BOUND", "CPU_BOUND", "MIXED" };
	char classes[256] = "";
	for (int i = 0; i < nt; i++) {
		int c = sched_ctl(SCHED_TASK_CLASS, tids[i], NULL);
		char one[40];
		snprintf(one, sizeof(one), "%s%s:%s", i ? "," : "", kinds[i] == 0 ? "cpu" : kinds[i] == 1 ? "io" : "probe",
			 c >= 0 && c < 6 ? cn[c] : "?");
		strlcat(classes, one, sizeof(classes));
	}
	struct fu_sysinfo mid;
	sysinfo(&mid);
	sleep_ms((uint64_t)secs * 500);
	stop = true;
	while (done_threads < ncpu + nio + 1)
		sleep_ms(5);
	uint64_t el = uptime_ns() - t0;
	sysinfo(&s1);
	long ov1 = proc_num("/proc/sched", "sched_overhead_ns");

	int n = nsamples;
	qsort_u64(wake_lat, n);
	qsort_u64(resp, n);
	uint64_t jobs = 0, sq = 0, iops = 0;
	for (int i = 0; i < ncpu; i++) {
		jobs += cpu_jobs[i];
		sq += cpu_jobs[i] * cpu_jobs[i];
	}
	for (int i = 0; i < nio; i++)
		iops += io_ops[i];
	/* Jain's fairness index over CPU workers: (sum x)^2 / (n * sum x^2), x1000 */
	uint64_t jain = sq ? jobs * jobs * 1000 / ((uint64_t)ncpu * sq) : 0;
	uint64_t cs = (s1.context_switches - s0.context_switches) * 1000000000UL / el;
	uint64_t busy = s1.busy_ns - s0.busy_ns, idle = s1.idle_ns - s0.idle_ns;
	printf("SCHEDBENCH {\"name\":\"%s\",\"policy\":\"%s\",\"seconds\":%d,\"cpu_workers\":%d,\"io_workers\":%d,"
	       "\"samples\":%d,\"wake_p50_us\":%lu,\"wake_p95_us\":%lu,\"wake_p99_us\":%lu,\"wake_max_us\":%lu,"
	       "\"resp_p50_us\":%lu,\"resp_p95_us\":%lu,\"resp_p99_us\":%lu,"
	       "\"cpu_jobs_per_s_x100\":%lu,\"io_ops_per_s\":%lu,\"jain_x1000\":%lu,\"ctx_switches_per_s\":%lu,"
	       "\"cpu_util_pct\":%lu,\"sched_overhead_ns_per_s\":%ld,\"system_class\":\"%s\",\"task_classes\":\"%s\"}\n",
	       name, pol, secs, ncpu, nio, n, pct(wake_lat, n, 50) / 1000, pct(wake_lat, n, 95) / 1000,
	       pct(wake_lat, n, 99) / 1000, n ? wake_lat[n - 1] / 1000 : 0, pct(resp, n, 50) / 1000,
	       pct(resp, n, 95) / 1000, pct(resp, n, 99) / 1000, jobs * 100000000000UL / el,
	       iops * 1000000000UL / el, jain, cs, (busy + idle) ? busy * 100 / (busy + idle) : 0,
	       (long)((ov1 - ov0) * 1000000000LL / (long)el), mid.system_class, classes);
	return 0;
}
