#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fuhrer/common.h"
#include "fuhrer/profiler.h"

/*
 * Per-process deltas need the previous counter value for every pid. A small
 * open-addressing table keyed by pid is enough: entries not seen in the
 * current scan are dropped by comparing the scan generation.
 */
#define PT_SIZE 8192

struct pt_entry {
	pid_t pid;
	uint32_t gen;
	uint64_t cpu_ticks;
	uint64_t io_ops;
};

struct fu_profiler {
	int scan_procs;
	uint32_t gen;
	struct pt_entry *table;
	char *buf;
	size_t buflen;
};

struct fu_profiler *fu_profiler_new(int scan_procs)
{
	struct fu_profiler *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->scan_procs = scan_procs;
	p->buflen = 1 << 16;
	p->buf = malloc(p->buflen);
	if (scan_procs)
		p->table = calloc(PT_SIZE, sizeof(struct pt_entry));
	if (!p->buf || (scan_procs && !p->table)) {
		fu_profiler_free(p);
		return NULL;
	}
	return p;
}

void fu_profiler_free(struct fu_profiler *p)
{
	if (!p)
		return;
	free(p->table);
	free(p->buf);
	free(p);
}

static struct pt_entry *pt_lookup(struct fu_profiler *p, pid_t pid, int *fresh)
{
	uint32_t h = ((uint32_t)pid * 2654435761u) & (PT_SIZE - 1);
	struct pt_entry *victim = NULL;
	for (int i = 0; i < PT_SIZE; i++) {
		struct pt_entry *e = &p->table[(h + i) & (PT_SIZE - 1)];
		if (e->pid == pid) {
			*fresh = 0;
			return e;
		}
		/* Stale slots (not seen for 2 scans) or empty ones are reusable. */
		if (!victim && (e->pid == 0 || p->gen - e->gen > 1))
			victim = e;
		if (e->pid == 0)
			break;
	}
	if (!victim)
		return NULL;
	memset(victim, 0, sizeof(*victim));
	victim->pid = pid;
	*fresh = 1;
	return victim;
}

int fu_parse_proc_stat(const char *buf, struct fu_sample *s)
{
	int found = 0;
	const char *line = buf;
	while (line && *line) {
		if (strncmp(line, "cpu ", 4) == 0) {
			struct fu_cpu_counters *c = &s->cpu;
			unsigned long long v[8] = {0};
			int n = sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu",
				       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
			if (n >= 4) {
				c->user = v[0]; c->nice = v[1]; c->system = v[2];
				c->idle = v[3]; c->iowait = v[4]; c->irq = v[5];
				c->softirq = v[6]; c->steal = v[7];
				found = 1;
			}
		} else if (strncmp(line, "ctxt ", 5) == 0) {
			s->ctxt = strtoull(line + 5, NULL, 10);
		} else if (strncmp(line, "procs_running ", 14) == 0) {
			s->procs_running = strtoull(line + 14, NULL, 10);
		} else if (strncmp(line, "procs_blocked ", 14) == 0) {
			s->procs_blocked = strtoull(line + 14, NULL, 10);
		}
		line = strchr(line, '\n');
		if (line)
			line++;
	}
	return found ? 0 : -1;
}

int fu_parse_meminfo(const char *buf, struct fu_sample *s)
{
	const char *p = strstr(buf, "MemTotal:");
	if (!p)
		return -1;
	s->mem_total_kb = strtoull(p + 9, NULL, 10);
	p = strstr(buf, "MemAvailable:");
	if (p) {
		s->mem_avail_kb = strtoull(p + 13, NULL, 10);
	} else {
		p = strstr(buf, "MemFree:");
		s->mem_avail_kb = p ? strtoull(p + 8, NULL, 10) : 0;
	}
	return 0;
}

int fu_parse_vmstat(const char *buf, struct fu_sample *s)
{
	const char *p = strstr(buf, "pgmajfault ");
	s->pgmajfault = p ? strtoull(p + 11, NULL, 10) : 0;
	return 0;
}

int fu_is_physical_disk(const char *name)
{
	size_t n = strlen(name);
	/* virtio (vda), SCSI/SATA (sda), Xen (xvda): letters only, no partition digit. */
	if ((strncmp(name, "vd", 2) == 0 || strncmp(name, "sd", 2) == 0 ||
	     strncmp(name, "hd", 2) == 0) && n >= 3) {
		for (size_t i = 2; i < n; i++)
			if (!islower((unsigned char)name[i]))
				return 0;
		return 1;
	}
	if (strncmp(name, "xvd", 3) == 0 && n >= 4) {
		for (size_t i = 3; i < n; i++)
			if (!islower((unsigned char)name[i]))
				return 0;
		return 1;
	}
	/* nvme0n1 yes, nvme0n1p1 no */
	if (strncmp(name, "nvme", 4) == 0)
		return strchr(name, 'p') == NULL;
	return 0;
}

int fu_parse_diskstats(const char *buf, struct fu_disk_counters *d)
{
	memset(d, 0, sizeof(*d));
	const char *line = buf;
	int ndisks = 0;
	while (line && *line) {
		unsigned maj, min;
		char name[64];
		unsigned long long v[11];
		int n = sscanf(line,
			       "%u %u %63s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
			       &maj, &min, name, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
			       &v[6], &v[7], &v[8], &v[9], &v[10]);
		if (n == 14 && fu_is_physical_disk(name)) {
			d->rd_ios += v[0]; d->rd_merges += v[1];
			d->rd_sectors += v[2]; d->rd_ticks += v[3];
			d->wr_ios += v[4]; d->wr_merges += v[5];
			d->wr_sectors += v[6]; d->wr_ticks += v[7];
			d->in_flight += v[8]; d->io_ticks += v[9];
			d->time_in_queue += v[10];
			ndisks++;
		}
		line = strchr(line, '\n');
		if (line)
			line++;
	}
	return ndisks;
}

int fu_parse_netdev(const char *buf, struct fu_net_counters *nc)
{
	memset(nc, 0, sizeof(*nc));
	int nif = 0;
	const char *line = buf;
	while (line && *line) {
		const char *colon = strchr(line, ':');
		const char *eol = strchr(line, '\n');
		if (colon && (!eol || colon < eol)) {
			const char *nm = line;
			while (*nm == ' ')
				nm++;
			size_t len = (size_t)(colon - nm);
			if (!(len == 2 && strncmp(nm, "lo", 2) == 0)) {
				unsigned long long v[16];
				int n = sscanf(colon + 1,
					       "%llu %llu %llu %llu %llu %llu %llu %llu "
					       "%llu %llu %llu %llu %llu %llu %llu %llu",
					       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
					       &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14],
					       &v[15]);
				if (n >= 10) {
					nc->rx_bytes += v[0];
					nc->rx_packets += v[1];
					nc->tx_bytes += v[8];
					nc->tx_packets += v[9];
					nif++;
				}
			}
		}
		line = eol ? eol + 1 : NULL;
	}
	return nif;
}

/* /proc/<pid>/stat: "pid (comm) state ppid ... utime stime" — comm may
 * contain spaces and parentheses, so parse from the last ')'. */
static int read_pid_stat(struct fu_profiler *p, const char *pid, char *comm,
			 uint64_t *ticks)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%s/stat", pid);
	if (fu_read_file(path, p->buf, p->buflen) <= 0)
		return -1;
	char *lp = strchr(p->buf, '(');
	char *rp = strrchr(p->buf, ')');
	if (!lp || !rp || rp < lp)
		return -1;
	size_t cl = (size_t)(rp - lp - 1);
	if (cl >= FU_COMM_LEN)
		cl = FU_COMM_LEN - 1;
	memcpy(comm, lp + 1, cl);
	comm[cl] = '\0';
	unsigned long long utime = 0, stime = 0;
	/* fields after ')': state(3) ppid pgrp session tty tpgid flags minflt
	 * cminflt majflt cmajflt utime(14) stime(15) */
	if (sscanf(rp + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu",
		   &utime, &stime) != 2)
		return -1;
	*ticks = utime + stime;
	return 0;
}

static int read_pid_io(struct fu_profiler *p, const char *pid, uint64_t *ops,
		       uint64_t *bytes)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%s/io", pid);
	if (fu_read_file(path, p->buf, p->buflen) <= 0)
		return -1;
	unsigned long long rchar = 0, wchar = 0, syscr = 0, syscw = 0;
	char *q;
	if ((q = strstr(p->buf, "rchar:"))) rchar = strtoull(q + 6, NULL, 10);
	if ((q = strstr(p->buf, "wchar:"))) wchar = strtoull(q + 6, NULL, 10);
	if ((q = strstr(p->buf, "syscr:"))) syscr = strtoull(q + 6, NULL, 10);
	if ((q = strstr(p->buf, "syscw:"))) syscw = strtoull(q + 6, NULL, 10);
	*ops = syscr + syscw;
	*bytes = rchar + wchar;
	return 0;
}

static void scan_processes(struct fu_profiler *p, struct fu_sample *s)
{
	char procdir[300];
	snprintf(procdir, sizeof(procdir), "%s/proc", fu_sys_root);
	DIR *d = opendir(procdir);
	if (!d)
		return;
	p->gen++;
	pid_t self = getpid();
	struct dirent *de;
	while ((de = readdir(d))) {
		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		pid_t pid = (pid_t)atoi(de->d_name);
		char comm[FU_COMM_LEN];
		uint64_t ticks = 0, ops = 0, bytes = 0;
		if (read_pid_stat(p, de->d_name, comm, &ticks) < 0)
			continue;
		int have_io = read_pid_io(p, de->d_name, &ops, &bytes) == 0;
		s->nprocs++;
		s->io_syscalls += ops;
		s->app_io_bytes += bytes;
		int fresh = 0;
		struct pt_entry *e = pt_lookup(p, pid, &fresh);
		if (!e)
			continue;
		if (!fresh && pid != self) {
			double dcpu = (double)(ticks - e->cpu_ticks);
			double dio = have_io ? (double)(ops - e->io_ops) : 0;
			if (dcpu > s->top_cpu.value) {
				s->top_cpu.value = dcpu;
				s->top_cpu.pid = pid;
				snprintf(s->top_cpu.comm, FU_COMM_LEN, "%s", comm);
			}
			if (dio > s->top_io.value) {
				s->top_io.value = dio;
				s->top_io.pid = pid;
				snprintf(s->top_io.comm, FU_COMM_LEN, "%s", comm);
			}
		}
		e->gen = p->gen;
		e->cpu_ticks = ticks;
		e->io_ops = ops;
	}
	closedir(d);
}

int fu_profiler_sample(struct fu_profiler *p, struct fu_sample *s)
{
	memset(s, 0, sizeof(*s));
	s->t = fu_now();
	if (fu_read_file("/proc/stat", p->buf, p->buflen) <= 0 ||
	    fu_parse_proc_stat(p->buf, s) < 0)
		return -1;
	if (fu_read_file("/proc/meminfo", p->buf, p->buflen) > 0)
		fu_parse_meminfo(p->buf, s);
	if (fu_read_file("/proc/vmstat", p->buf, p->buflen) > 0)
		fu_parse_vmstat(p->buf, s);
	if (fu_read_file("/proc/diskstats", p->buf, p->buflen) > 0)
		fu_parse_diskstats(p->buf, &s->disk);
	if (fu_read_file("/proc/net/dev", p->buf, p->buflen) > 0)
		fu_parse_netdev(p->buf, &s->net);
	if (p->scan_procs)
		scan_processes(p, s);
	return 0;
}
