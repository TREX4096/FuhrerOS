#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "fuhrer/features.h"

static const char *class_names[FU_CLASS_COUNT] = {
	[FU_CLASS_IDLE] = "IDLE",
	[FU_CLASS_INTERACTIVE] = "INTERACTIVE",
	[FU_CLASS_CPU_BOUND] = "CPU_BOUND",
	[FU_CLASS_IO_SEQUENTIAL] = "IO_SEQUENTIAL",
	[FU_CLASS_IO_RANDOM] = "IO_RANDOM",
	[FU_CLASS_NETWORK_HEAVY] = "NETWORK_HEAVY",
	[FU_CLASS_MIXED] = "MIXED",
};

const char *fu_class_name(enum fu_class c)
{
	return (c >= 0 && c < FU_CLASS_COUNT) ? class_names[c] : "UNKNOWN";
}

int fu_class_from_name(const char *name)
{
	for (int i = 0; i < FU_CLASS_COUNT; i++)
		if (strcasecmp(name, class_names[i]) == 0)
			return i;
	return -1;
}

void fu_thresholds_default(struct fu_thresholds *t)
{
	t->cpu_bound_pct = 80.0;
	t->io_active_iops = 200.0;
	t->io_active_mbps = 8.0;
	t->app_io_active_ops = 5000.0;
	t->small_io_kb = 16.0;
	t->large_io_kb = 128.0;
	t->seq_merge_ratio = 0.4;
	t->net_active_pps = 3000.0;
	t->net_active_mbps = 5.0;
	t->interactive_ctxsw = 500.0;
}

static double rate(uint64_t a, uint64_t b, double dt)
{
	return b >= a ? (double)(b - a) / dt : 0.0;
}

void fu_features_compute(const struct fu_sample *p, const struct fu_sample *c,
			 int ncpus, struct fu_features *f)
{
	memset(f, 0, sizeof(*f));
	double dt = c->t - p->t;
	if (dt <= 0)
		dt = 1e-3;
	f->interval_s = dt;
	f->ncpus = ncpus > 0 ? ncpus : 1;

	const struct fu_cpu_counters *a = &p->cpu, *b = &c->cpu;
	double busy = (double)((b->user - a->user) + (b->nice - a->nice) +
			       (b->system - a->system) + (b->irq - a->irq) +
			       (b->softirq - a->softirq));
	double iow = (double)(b->iowait - a->iowait);
	double idle = (double)(b->idle - a->idle);
	double steal = (double)(b->steal - a->steal);
	double total = busy + iow + idle + steal;
	if (total > 0) {
		f->cpu_pct = 100.0 * busy / total;
		f->iowait_pct = 100.0 * iow / total;
	}
	if (c->mem_total_kb)
		f->mem_used_pct = 100.0 * (double)(c->mem_total_kb - c->mem_avail_kb) /
				  (double)c->mem_total_kb;
	f->ctxsw_per_s = rate(p->ctxt, c->ctxt, dt);
	f->run_queue = (double)c->procs_running;
	f->blocked = (double)c->procs_blocked;
	f->majfault_per_s = rate(p->pgmajfault, c->pgmajfault, dt);

	const struct fu_disk_counters *da = &p->disk, *db = &c->disk;
	double rd = rate(da->rd_ios, db->rd_ios, dt);
	double wr = rate(da->wr_ios, db->wr_ios, dt);
	double rmerge = rate(da->rd_merges, db->rd_merges, dt);
	double wmerge = rate(da->wr_merges, db->wr_merges, dt);
	double rsec = rate(da->rd_sectors, db->rd_sectors, dt);
	double wsec = rate(da->wr_sectors, db->wr_sectors, dt);
	f->read_iops = rd;
	f->write_iops = wr;
	f->iops = rd + wr;
	f->read_mbps = rsec * 512.0 / 1e6;
	f->write_mbps = wsec * 512.0 / 1e6;
	if (f->iops > 0) {
		f->avg_io_kb = (rsec + wsec) * 512.0 / 1024.0 / f->iops;
		f->read_ratio = rd / f->iops;
		f->merge_ratio = (rmerge + wmerge) / (f->iops + rmerge + wmerge);
	}
	/* time_in_queue and io_ticks are in milliseconds */
	f->queue_depth = rate(da->time_in_queue, db->time_in_queue, dt) / 1000.0;
	f->disk_util_pct = rate(da->io_ticks, db->io_ticks, dt) / 10.0;
	if (f->disk_util_pct > 100.0)
		f->disk_util_pct = 100.0;

	const struct fu_net_counters *na = &p->net, *nb = &c->net;
	f->rx_mbps = rate(na->rx_bytes, nb->rx_bytes, dt) / 1e6;
	f->tx_mbps = rate(na->tx_bytes, nb->tx_bytes, dt) / 1e6;
	f->rx_pps = rate(na->rx_packets, nb->rx_packets, dt);
	f->tx_pps = rate(na->tx_packets, nb->tx_packets, dt);
	double pps = f->rx_pps + f->tx_pps;
	if (pps > 0)
		f->avg_pkt_bytes = (f->rx_mbps + f->tx_mbps) * 1e6 / pps;

	f->io_syscalls_per_s = rate(p->io_syscalls, c->io_syscalls, dt);
	f->app_io_mbps = rate(p->app_io_bytes, c->app_io_bytes, dt) / 1e6;

	/* top_* hold interval deltas (ticks / ops); convert to per-second. */
	f->top_cpu = c->top_cpu;
	f->top_io = c->top_io;
	long hz = sysconf(_SC_CLK_TCK);
	if (hz <= 0)
		hz = 100;
	f->top_cpu.value = 100.0 * c->top_cpu.value / (double)hz / dt; /* % of one core */
	f->top_io.value = c->top_io.value / dt;
}

enum fu_class fu_classify(const struct fu_features *f, const struct fu_thresholds *t,
			  const char **reason)
{
	const char *why = "low_activity";
	enum fu_class cls;

	int disk_active = f->iops >= t->io_active_iops ||
			  (f->read_mbps + f->write_mbps) >= t->io_active_mbps;
	int net_active = (f->rx_pps + f->tx_pps) >= t->net_active_pps ||
			 (f->rx_mbps + f->tx_mbps) >= t->net_active_mbps;
	int app_io_active = f->io_syscalls_per_s >= t->app_io_active_ops;
	/* A single saturated thread on a multi-vCPU guest is still CPU bound. */
	int cpu_busy = f->cpu_pct >= t->cpu_bound_pct || f->top_cpu.value >= 90.0;

	if (disk_active) {
		if (f->avg_io_kb <= t->small_io_kb && f->merge_ratio < t->seq_merge_ratio) {
			cls = FU_CLASS_IO_RANDOM;
			why = "high_small_io";
		} else if (f->avg_io_kb >= t->large_io_kb ||
			   f->merge_ratio >= t->seq_merge_ratio) {
			cls = FU_CLASS_IO_SEQUENTIAL;
			why = "large_sequential_io";
		} else {
			cls = FU_CLASS_MIXED;
			why = "medium_block_io";
		}
		if (net_active) {
			cls = FU_CLASS_MIXED;
			why = "disk_and_network";
		}
	} else if (net_active) {
		cls = FU_CLASS_NETWORK_HEAVY;
		why = f->avg_pkt_bytes > 0 && f->avg_pkt_bytes < 512 ? "small_packet_rate"
								      : "network_throughput";
	} else if (app_io_active) {
		/* Page-cache-served I/O: no block traffic, but many syscalls. */
		double avg_kb = f->app_io_mbps * 1e6 / 1024.0 / f->io_syscalls_per_s;
		if (avg_kb <= t->small_io_kb) {
			cls = FU_CLASS_IO_RANDOM;
			why = "cached_small_io";
		} else {
			cls = FU_CLASS_IO_SEQUENTIAL;
			why = "cached_large_io";
		}
	} else if (cpu_busy) {
		cls = FU_CLASS_CPU_BOUND;
		why = "cpu_saturated";
	} else if (f->cpu_pct >= 5.0 || f->ctxsw_per_s >= t->interactive_ctxsw) {
		cls = FU_CLASS_INTERACTIVE;
		why = f->ctxsw_per_s >= t->interactive_ctxsw ? "frequent_wakeups" : "light_load";
	} else {
		cls = FU_CLASS_IDLE;
	}
	if (reason)
		*reason = why;
	return cls;
}
