/*
 * FuhrerOS workload profiler (M2).
 *
 * Takes cheap snapshots of kernel counters from procfs. All rates are derived
 * later by the feature extractor from two consecutive snapshots, so the
 * profiler itself does no arithmetic beyond summing devices.
 */
#ifndef FUHRER_PROFILER_H
#define FUHRER_PROFILER_H

#include <stdint.h>
#include <sys/types.h>

#define FU_COMM_LEN 32

struct fu_cpu_counters {
	uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
};

struct fu_disk_counters {
	uint64_t rd_ios, rd_merges, rd_sectors, rd_ticks;
	uint64_t wr_ios, wr_merges, wr_sectors, wr_ticks;
	uint64_t in_flight, io_ticks, time_in_queue;
};

struct fu_net_counters {
	uint64_t rx_bytes, rx_packets, tx_bytes, tx_packets;
};

/* Per-process hot spot identified during a sample. */
struct fu_proc_top {
	pid_t pid;
	char comm[FU_COMM_LEN];
	double value; /* CPU ticks or I/O ops in the interval */
};

struct fu_sample {
	double t;			/* monotonic seconds */
	struct fu_cpu_counters cpu;
	uint64_t ctxt;			/* context switches since boot */
	uint64_t procs_running, procs_blocked;
	uint64_t mem_total_kb, mem_avail_kb;
	uint64_t pgmajfault;
	struct fu_disk_counters disk;	/* summed over physical disks */
	struct fu_net_counters net;	/* summed over non-loopback ifaces */
	/* Application-level I/O syscalls (sum of /proc/<pid>/io syscr+syscw).
	 * Only valid when per-process scanning is enabled. */
	uint64_t io_syscalls;
	uint64_t app_io_bytes;
	struct fu_proc_top top_cpu;	/* busiest process in the interval */
	struct fu_proc_top top_io;	/* most I/O syscalls in the interval */
	int nprocs;
};

struct fu_profiler;

struct fu_profiler *fu_profiler_new(int scan_procs);
void fu_profiler_free(struct fu_profiler *p);
/* Fill s. Returns 0 on success, -1 if a mandatory source is missing. */
int fu_profiler_sample(struct fu_profiler *p, struct fu_sample *s);

/* Parsers are exported for unit tests. */
int fu_parse_proc_stat(const char *buf, struct fu_sample *s);
int fu_parse_meminfo(const char *buf, struct fu_sample *s);
int fu_parse_vmstat(const char *buf, struct fu_sample *s);
int fu_parse_diskstats(const char *buf, struct fu_disk_counters *d);
int fu_parse_netdev(const char *buf, struct fu_net_counters *n);
/* Is this /proc/diskstats device name a whole physical disk we care about? */
int fu_is_physical_disk(const char *name);

#endif
