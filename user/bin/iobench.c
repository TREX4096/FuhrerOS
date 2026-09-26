/* iobench - buffer-cache policy evaluation (NEW_EXPLANATION §31, M14/M16).
 *
 *   iobench [-p POLICY] [-w WORKLOAD] [-m FILE_MB] [-c CACHE_BLOCKS] [-t SECONDS]
 * workloads:
 *   seq      read the file front to back, repeatedly
 *   random   uniformly random 4 KiB reads
 *   hotset   80 % of reads hit a 2 MiB hot region, 20 % random (working set)
 *   scan     hotset traffic interleaved with a large sequential scan (the
 *            classic case where LRU lets a scan flush the working set)
 * The cache is dropped before each run and its capacity set so the file
 * does not fit. Output: IOBENCH {json} with throughput, mean and P99 read
 * latency, hit rate, read-ahead usefulness and cache CPU time. */
#include "fu.h"

#define MAXS 200000
static uint64_t lat[MAXS];

static long pnum(const char *path, const char *key)
{
	static char b[4096];
	read_file(path, b, sizeof(b));
	return text_num(b, key);
}

int main(int argc, char **argv)
{
	const char *policy = NULL, *wl = "random";
	int mb = 64, cap = 1024, secs = 8;
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "-p")) policy = argv[i + 1];
		else if (!strcmp(argv[i], "-w")) wl = argv[i + 1];
		else if (!strcmp(argv[i], "-m")) mb = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-c")) cap = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-t")) secs = atoi(argv[i + 1]);
	}
	const char *path = "/home/.iobench.dat";
	struct stat st;
	if (stat(path, &st) < 0 || st.size < (uint64_t)mb << 20) {
		printf("iobench: creating %d MiB test file...\n", mb);
		int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
		static uint8_t blk[65536];
		for (int i = 0; i < mb * 16; i++) {
			for (int j = 0; j < 65536; j += 512)
				blk[j] = (uint8_t)(i + j);
			write(fd, blk, sizeof(blk));
		}
		close(fd);
		sync();
	}
	if (policy)
		sys3(SYS_CACHE_CTL, CACHE_SET_POLICY, 0, policy);
	sys3(SYS_CACHE_CTL, CACHE_SET_CAPACITY, cap, 0);
	sys3(SYS_CACHE_CTL, CACHE_DROP, 0, 0);
	sys3(SYS_CACHE_CTL, CACHE_RESET_STATS, 0, 0);
	long cpu0 = pnum("/proc/bcache", "cpu_ns");

	int fd = open(path, O_RDONLY);
	uint64_t blocks = ((uint64_t)mb << 20) / 4096, hot = 512; /* 2 MiB hot set */
	static char buf[4096];
	uint64_t t0 = uptime_ns(), end = t0 + (uint64_t)secs * 1000000000UL;
	long n = 0;
	uint64_t pos = 0, scanpos = 0, bytes = 0;
	while (uptime_ns() < end) {
		uint64_t b;
		if (!strcmp(wl, "seq"))
			b = pos++ % blocks;
		else if (!strcmp(wl, "hotset"))
			b = (rand32() % 100) < 80 ? rand32() % hot : rand32() % blocks;
		else if (!strcmp(wl, "scan"))
			b = (n % 2) ? (hot + scanpos++ % (blocks - hot)) : rand32() % hot;
		else
			b = rand32() % blocks;
		uint64_t r0 = uptime_ns();
		lseek(fd, (long)(b * 4096), SEEK_SET);
		ssize_t k = read(fd, buf, sizeof(buf));
		uint64_t d = uptime_ns() - r0;
		if (k > 0)
			bytes += (uint64_t)k;
		if (n < MAXS)
			lat[n] = d;
		n++;
	}
	uint64_t el = uptime_ns() - t0;
	close(fd);
	long m = MIN(n, MAXS);
	uint64_t sum = 0;
	for (long i = 0; i < m; i++)
		sum += lat[i];
	qsort_u64(lat, m);
	struct fu_blkstat bs;
	sys1(SYS_BLKSTAT, &bs);
	long cpu1 = pnum("/proc/bcache", "cpu_ns");
	uint64_t acc = bs.cache_hits + bs.cache_misses;
	printf("IOBENCH {\"policy\":\"%s\",\"workload\":\"%s\",\"file_mb\":%d,\"cache_blocks\":%d,\"seconds\":%d,"
	       "\"reads\":%ld,\"mb_per_s_x100\":%lu,\"lat_mean_us\":%lu,\"lat_p50_us\":%lu,\"lat_p99_us\":%lu,"
	       "\"hit_rate_pm\":%lu,\"readahead_issued\":%lu,\"readahead_used\":%lu,\"evictions\":%lu,"
	       "\"disk_reads\":%lu,\"cache_cpu_us\":%ld,\"cache_cpu_ns_per_read\":%ld,\"io_class\":\"%s\"}\n",
	       /* KiB first: bytes * 1e11 overflowed above ~180 MB (F-118) */
	       bs.cache_policy, wl, mb, cap, secs, n, (bytes / 1024) * 100000000000UL / el / 1024,
	       m ? sum / (uint64_t)m / 1000 : 0, m ? lat[m / 2] / 1000 : 0, m ? lat[(m - 1) * 99 / 100] / 1000 : 0,
	       acc ? bs.cache_hits * 1000 / acc : 0, bs.readahead_blocks, bs.readahead_hits, bs.evictions,
	       bs.reads, (cpu1 - cpu0) / 1000, n ? (cpu1 - cpu0) / n : 0, bs.io_class);
	return 0;
}
