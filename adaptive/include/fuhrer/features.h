/*
 * FuhrerOS feature extraction and rule-based workload classification (M3).
 */
#ifndef FUHRER_FEATURES_H
#define FUHRER_FEATURES_H

#include "fuhrer/profiler.h"

struct fu_features {
	double interval_s;
	double cpu_pct;		/* non-idle, non-iowait share of all CPUs */
	double iowait_pct;
	double mem_used_pct;
	double ctxsw_per_s;
	double run_queue;	/* procs_running at sample time */
	double blocked;		/* procs in uninterruptible sleep */
	double read_iops, write_iops, iops;
	double read_mbps, write_mbps;
	double avg_io_kb;	/* mean request size on the block device */
	double read_ratio;	/* reads / (reads + writes), 0..1 */
	double merge_ratio;	/* merged / (ios + merged): adjacency => sequential */
	double queue_depth;	/* avg requests in flight (time_in_queue / wall) */
	double disk_util_pct;
	double rx_mbps, tx_mbps, rx_pps, tx_pps, avg_pkt_bytes;
	double io_syscalls_per_s;	/* app-level read/write syscalls */
	double app_io_mbps;
	double majfault_per_s;
	int ncpus;
	struct fu_proc_top top_cpu, top_io;
};

enum fu_class {
	FU_CLASS_IDLE = 0,
	FU_CLASS_INTERACTIVE,
	FU_CLASS_CPU_BOUND,
	FU_CLASS_IO_SEQUENTIAL,
	FU_CLASS_IO_RANDOM,
	FU_CLASS_NETWORK_HEAVY,
	FU_CLASS_MIXED,
	FU_CLASS_COUNT
};

struct fu_thresholds {
	double cpu_bound_pct;		/* CPU% at/above which CPU is "busy" */
	double io_active_iops;		/* block IOPS for "doing I/O" */
	double io_active_mbps;		/* or block MB/s */
	double app_io_active_ops;	/* or app-level I/O syscalls/s (page-cache hits) */
	double small_io_kb;		/* avg request <= this => small */
	double large_io_kb;		/* avg request >= this => large */
	double seq_merge_ratio;		/* merge ratio >= this => sequential */
	double net_active_pps;
	double net_active_mbps;
	double interactive_ctxsw;	/* ctx switches/s suggesting interactivity */
};

void fu_thresholds_default(struct fu_thresholds *t);

/* Derive rates from two samples. ncpus scales the CPU percentage. */
void fu_features_compute(const struct fu_sample *prev, const struct fu_sample *cur,
			 int ncpus, struct fu_features *f);

/* Classify; *reason receives a short static identifier (e.g. "high_small_io"). */
enum fu_class fu_classify(const struct fu_features *f, const struct fu_thresholds *t,
			  const char **reason);

const char *fu_class_name(enum fu_class c);
int fu_class_from_name(const char *name);

#endif
