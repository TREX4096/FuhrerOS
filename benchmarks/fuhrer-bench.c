/*
 * fuhrer-bench — FuhrerOS benchmark suite (INSTRUCTION §20, M5/M15).
 *
 *   fuhrer-bench storage  [--pattern rand|seq] [--op read|write] [--bs 4k]
 *                         [--size 256M] [--time 10] [--depth 32]
 *                         [--backend auto|normal|batched|specialized]
 *                         [--file PATH] [--drop-cache]
 *   fuhrer-bench cpu      [--time 10] [--threads N]
 *   fuhrer-bench memory   [--size 64M] [--time 10]
 *   fuhrer-bench net      [--mode latency|throughput] [--msg 64] [--time 10]
 *   fuhrer-bench transition [--phase-time 15] [--file PATH]
 *
 * Common: --warmup S   --json PATH|-   --timeline PATH (per-second CSV)
 *
 * Every run reports medians/percentiles computed from raw per-operation
 * samples; nothing is estimated.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "fuhrer.h"

/* ---------- options ---------- */

struct opts {
	const char *cmd;
	const char *pattern, *op, *file, *json, *timeline, *mode;
	size_t bs, size, msg;
	double time, warmup, phase_time;
	int depth, threads, backend, drop_cache;
};

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static size_t parse_size(const char *s)
{
	char *end;
	double v = strtod(s, &end);
	switch (*end) {
	case 'k': case 'K': v *= 1024; break;
	case 'm': case 'M': v *= 1024 * 1024; break;
	case 'g': case 'G': v *= 1024.0 * 1024 * 1024; break;
	}
	return (size_t)v;
}

/* ---------- sample reservoir + stats ---------- */

struct samples {
	double *v;
	size_t n, cap;
	uint64_t seen;
	uint64_t rng;
};

static void samples_init(struct samples *s, size_t cap)
{
	memset(s, 0, sizeof(*s));
	s->cap = cap;
	s->v = malloc(cap * sizeof(double));
	s->rng = 0x9E3779B97F4A7C15ull;
}

static uint64_t xorshift(uint64_t *x)
{
	*x ^= *x << 13;
	*x ^= *x >> 7;
	*x ^= *x << 17;
	return *x;
}

static void samples_add(struct samples *s, double x)
{
	s->seen++;
	if (s->n < s->cap) {
		s->v[s->n++] = x;
		return;
	}
	uint64_t j = xorshift(&s->rng) % s->seen; /* reservoir sampling */
	if (j < s->cap)
		s->v[j] = x;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

struct stats {
	double p50, p95, p99, max, mean, stdev;
	size_t n;
};

static struct stats samples_stats(struct samples *s)
{
	struct stats st = { 0 };
	st.n = s->n;
	if (!s->n)
		return st;
	qsort(s->v, s->n, sizeof(double), cmp_double);
	double sum = 0, sq = 0;
	for (size_t i = 0; i < s->n; i++)
		sum += s->v[i];
	st.mean = sum / (double)s->n;
	for (size_t i = 0; i < s->n; i++)
		sq += (s->v[i] - st.mean) * (s->v[i] - st.mean);
	st.stdev = s->n > 1 ? sqrt(sq / (double)(s->n - 1)) : 0;
	st.p50 = s->v[(size_t)(0.50 * (double)(s->n - 1))];
	st.p95 = s->v[(size_t)(0.95 * (double)(s->n - 1))];
	st.p99 = s->v[(size_t)(0.99 * (double)(s->n - 1))];
	st.max = s->v[s->n - 1];
	return st;
}

/* ---------- JSON output ---------- */

static FILE *json_out;

static void json_begin(const struct opts *o)
{
	json_out = NULL;
	if (!o->json)
		return;
	json_out = !strcmp(o->json, "-") ? stdout : fopen(o->json, "w");
	if (!json_out) {
		perror(o->json);
		return;
	}
	struct utsname u;
	uname(&u);
	int cls = -1, mode = -1;
	int pol = fu_current_policy(&cls, &mode);
	fprintf(json_out,
		"{\n  \"tool\": \"fuhrer-bench\",\n  \"workload\": \"%s\",\n"
		"  \"kernel\": \"%s\",\n  \"ncpus\": %ld,\n  \"daemon_policy_at_start\": %d,\n"
		"  \"daemon_mode_at_start\": %d,\n  \"timestamp\": %ld",
		o->cmd, u.release, sysconf(_SC_NPROCESSORS_ONLN), pol, mode, (long)time(NULL));
}

static void json_num(const char *k, double v)
{
	if (json_out)
		fprintf(json_out, ",\n  \"%s\": %.6g", k, v);
}

static void json_str(const char *k, const char *v)
{
	if (json_out)
		fprintf(json_out, ",\n  \"%s\": \"%s\"", k, v);
}

static void json_stats(const char *k, const struct stats *s)
{
	if (json_out)
		fprintf(json_out,
			",\n  \"%s\": {\"n\": %zu, \"mean\": %.6g, \"stdev\": %.6g, \"p50\": %.6g, "
			"\"p95\": %.6g, \"p99\": %.6g, \"max\": %.6g}",
			k, s->n, s->mean, s->stdev, s->p50, s->p95, s->p99, s->max);
}

static void json_end(void)
{
	if (!json_out)
		return;
	fprintf(json_out, "\n}\n");
	if (json_out != stdout)
		fclose(json_out);
	json_out = NULL;
}

static double cpu_time(double *user, double *sys)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	*user = (double)ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6;
	*sys = (double)ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
	return *user + *sys;
}

/* ---------- timeline ---------- */

static FILE *timeline;

static void timeline_open(const char *path)
{
	timeline = path ? fopen(path, "w") : NULL;
	if (timeline)
		fprintf(timeline, "t,phase,ops,mb,backend,daemon_policy,daemon_class\n");
}

static void timeline_row(double t, const char *phase, double ops, double mb, int backend)
{
	if (!timeline)
		return;
	int cls = -1;
	int pol = fu_current_policy(&cls, NULL);
	fprintf(timeline, "%.2f,%s,%.0f,%.3f,%s,%d,%d\n", t, phase, ops, mb,
		fu_backend_name(backend), pol, cls);
	fflush(timeline);
}

/* ---------- storage ---------- */

static int prepare_file(const char *path, size_t size)
{
	struct stat st;
	if (stat(path, &st) == 0 && (size_t)st.st_size >= size)
		return 0;
	int fd = open(path, O_WRONLY | O_CREAT, 0644);
	if (fd < 0)
		return -1;
	size_t chunk = 1 << 20;
	char *buf = malloc(chunk);
	uint64_t x = 0x1234567;
	for (size_t i = 0; i < chunk / 8; i++)
		((uint64_t *)buf)[i] = xorshift(&x);
	for (size_t off = 0; off < size; off += chunk) {
		size_t n = size - off < chunk ? size - off : chunk;
		if (pwrite(fd, buf, n, (off_t)off) != (ssize_t)n) {
			free(buf);
			close(fd);
			return -1;
		}
	}
	free(buf);
	fsync(fd);
	close(fd);
	return 0;
}

static void drop_caches(void)
{
	sync();
	int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
	if (fd >= 0) {
		if (write(fd, "3", 1) < 0)
			perror("drop_caches");
		close(fd);
	}
}

struct storage_run {
	double ops, bytes, secs;
	struct samples lat;
	struct fu_io_stats io;
};

/* Run a storage loop for `secs`; if tl_phase != NULL emit per-second rows. */
static int storage_loop(const struct opts *o, double secs, struct storage_run *r,
			const char *tl_phase, double tl_base, int record)
{
	int wr = !strcmp(o->op, "write");
	int seq = !strcmp(o->pattern, "seq");
	struct fu_io *io = fu_io_open(o->file, wr ? O_RDWR : O_RDONLY, 0, o->backend, o->depth);
	if (!io) {
		fprintf(stderr, "open %s: %s\n", o->file, strerror(errno));
		return -1;
	}
	int depth = o->depth;
	struct fu_req *reqs = calloc((size_t)depth, sizeof(*reqs));
	char **bufs = calloc((size_t)depth, sizeof(char *));
	for (int i = 0; i < depth; i++) {
		bufs[i] = fu_io_alloc(o->bs);
		memset(bufs[i], 0xA5 ^ i, o->bs);
	}
	size_t nblocks = o->size / o->bs;
	if (!nblocks)
		nblocks = 1;
	uint64_t rng = 0xC0FFEE ^ (uint64_t)getpid() ^ (uint64_t)(now_s() * 1e6);
	/* The sequential cursor persists across calls so the measured phase
	 * never re-reads blocks the warm-up phase just pulled into the cache. */
	static uint64_t cursor;
	double t0 = now_s(), tick = t0, tick_ops = 0, tick_bytes = 0;
	double end = t0 + secs;
	while (now_s() < end) {
		for (int i = 0; i < depth; i++) {
			uint64_t blk = seq ? (cursor++ % nblocks) : xorshift(&rng) % nblocks;
			reqs[i].op = wr ? FU_OP_WRITE : FU_OP_READ;
			reqs[i].buf = bufs[i];
			reqs[i].len = o->bs;
			reqs[i].off = (off_t)(blk * o->bs);
		}
		int ok = fu_io_run(io, reqs, depth);
		if (ok < 0) {
			fprintf(stderr, "I/O error: %s\n", strerror(-ok));
			break;
		}
		for (int i = 0; i < depth; i++) {
			if (reqs[i].res < 0) {
				fprintf(stderr, "request failed: %s\n", strerror((int)-reqs[i].res));
				continue;
			}
			if (record) {
				r->ops++;
				r->bytes += (double)reqs[i].res;
				samples_add(&r->lat, reqs[i].lat_us);
			}
			tick_ops++;
			tick_bytes += (double)reqs[i].res;
		}
		double t = now_s();
		if (t - tick >= 1.0) {
			if (tl_phase)
				timeline_row(tl_base + (t - t0), tl_phase, tick_ops / (t - tick),
					     tick_bytes / 1e6 / (t - tick), fu_io_backend_last(io));
			tick = t;
			tick_ops = tick_bytes = 0;
		}
	}
	if (wr)
		fu_io_fsync(io);
	r->secs += now_s() - t0;
	fu_io_get_stats(io, &r->io);
	for (int i = 0; i < depth; i++)
		free(bufs[i]);
	free(bufs);
	free(reqs);
	fu_io_close(io);
	return 0;
}

static int bench_storage(struct opts *o)
{
	if (!o->file)
		o->file = "/var/tmp/fuhrer-bench.dat";
	printf("preparing %s (%zu MiB)...\n", o->file, o->size >> 20);
	if (prepare_file(o->file, o->size) < 0) {
		perror("prepare");
		return 1;
	}
	if (o->drop_cache)
		drop_caches();
	struct storage_run warm = { 0 }, run = { 0 };
	samples_init(&warm.lat, 16);
	samples_init(&run.lat, 1 << 20);
	if (o->warmup > 0)
		storage_loop(o, o->warmup, &warm, NULL, 0, 0);
	double u0, s0, u1, s1;
	cpu_time(&u0, &s0);
	timeline_open(o->timeline);
	if (storage_loop(o, o->time, &run, o->timeline ? "storage" : NULL, 0, 1) < 0)
		return 1;
	cpu_time(&u1, &s1);
	struct stats st = samples_stats(&run.lat);
	double iops = run.ops / run.secs, mbps = run.bytes / 1e6 / run.secs;
	printf("storage %s-%s bs=%zu depth=%d backend=%s: %.0f IOPS, %.2f MB/s\n"
	       "  latency us: p50=%.1f p95=%.1f p99=%.1f max=%.1f  (n=%zu)\n"
	       "  cpu: user=%.2fs sys=%.2fs  backend ops normal/batched/specialized=%llu/%llu/%llu "
	       "switches=%llu\n",
	       o->pattern, o->op, o->bs, o->depth, fu_backend_name(o->backend), iops, mbps, st.p50,
	       st.p95, st.p99, st.max, st.n, u1 - u0, s1 - s0,
	       (unsigned long long)run.io.ops[0], (unsigned long long)run.io.ops[1],
	       (unsigned long long)run.io.ops[2], (unsigned long long)run.io.backend_switches);
	json_begin(o);
	json_str("pattern", o->pattern);
	json_str("op", o->op);
	json_num("bs", (double)o->bs);
	json_num("depth", o->depth);
	json_num("file_size", (double)o->size);
	json_str("backend", fu_backend_name(o->backend));
	json_num("duration_s", run.secs);
	json_num("ops", run.ops);
	json_num("iops", iops);
	json_num("mbps", mbps);
	json_stats("latency_us", &st);
	json_num("cpu_user_s", u1 - u0);
	json_num("cpu_sys_s", s1 - s0);
	json_num("cpu_per_op_us", (u1 - u0 + s1 - s0) * 1e6 / (run.ops > 0 ? run.ops : 1));
	json_num("backend_ops_normal", (double)run.io.ops[0]);
	json_num("backend_ops_batched", (double)run.io.ops[1]);
	json_num("backend_ops_specialized", (double)run.io.ops[2]);
	json_num("backend_switches", (double)run.io.backend_switches);
	json_num("direct_fallbacks", (double)run.io.direct_fallbacks);
	json_str("unit_throughput", "MB/s (1e6 bytes)");
	json_end();
	return 0;
}

/* ---------- CPU ---------- */

struct cpu_arg {
	double secs;
	double ops;
};

static void *cpu_worker(void *p)
{
	struct cpu_arg *a = p;
	uint64_t x = 88172645463325252ull, acc = 0;
	double end = now_s() + a->secs;
	double ops = 0;
	while (now_s() < end) {
		for (int i = 0; i < 100000; i++) {
			x ^= x << 13; x ^= x >> 7; x ^= x << 17;
			acc += x * 0x9E3779B97F4A7C15ull;
		}
		ops += 100000;
	}
	a->ops = ops + (double)(acc & 1) * 0; /* keep acc live */
	__asm__ volatile("" : : "r"(acc));
	return NULL;
}

static double cpu_run(int threads, double secs)
{
	pthread_t th[256];
	struct cpu_arg args[256];
	if (threads > 256)
		threads = 256;
	for (int i = 0; i < threads; i++) {
		args[i].secs = secs;
		pthread_create(&th[i], NULL, cpu_worker, &args[i]);
	}
	double total = 0;
	for (int i = 0; i < threads; i++) {
		pthread_join(th[i], NULL);
		total += args[i].ops;
	}
	return total / secs;
}

static int bench_cpu(struct opts *o)
{
	if (o->threads <= 0)
		o->threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (o->warmup > 0)
		cpu_run(o->threads, o->warmup);
	/* One-second slices give a distribution instead of a single number. */
	struct samples s;
	samples_init(&s, 4096);
	int slices = (int)(o->time + 0.5);
	if (slices < 1)
		slices = 1;
	timeline_open(o->timeline);
	for (int i = 0; i < slices; i++) {
		double r = cpu_run(o->threads, 1.0) / 1e6;
		samples_add(&s, r);
		timeline_row(i + 1, "cpu", r * 1e6, 0, 0);
	}
	struct stats st = samples_stats(&s);
	printf("cpu threads=%d: median %.1f Mops/s (p5..max spread stdev %.2f)\n", o->threads,
	       st.p50, st.stdev);
	json_begin(o);
	json_num("threads", o->threads);
	json_stats("mops_per_s", &st);
	json_end();
	return 0;
}

/* ---------- memory ---------- */

static int bench_memory(struct opts *o)
{
	size_t n = o->size / sizeof(double) / 3;
	double *a = malloc(n * sizeof(double)), *b = malloc(n * sizeof(double)),
	       *c = malloc(n * sizeof(double));
	if (!a || !b || !c)
		return 1;
	for (size_t i = 0; i < n; i++) {
		a[i] = 1.0;
		b[i] = 2.0;
		c[i] = 0.0;
	}
	struct samples bw;
	samples_init(&bw, 4096);
	double end = now_s() + o->time;
	while (now_s() < end) {
		double t0 = now_s();
		for (size_t i = 0; i < n; i++)
			c[i] = a[i] + 3.0 * b[i]; /* STREAM triad */
		double dt = now_s() - t0;
		samples_add(&bw, 3.0 * (double)n * sizeof(double) / dt / 1e9);
	}
	/* Pointer chase over a random cyclic permutation: dependent loads. */
	size_t m = o->size / sizeof(size_t);
	size_t *next = malloc(m * sizeof(size_t));
	for (size_t i = 0; i < m; i++)
		next[i] = i;
	uint64_t rng = 42;
	for (size_t i = m - 1; i > 0; i--) { /* Sattolo: single cycle */
		size_t j = xorshift(&rng) % i;
		size_t t = next[i]; next[i] = next[j]; next[j] = t;
	}
	size_t p = 0, hops = 2000000;
	double t0 = now_s();
	for (size_t i = 0; i < hops; i++)
		p = next[p];
	double chase_ns = (now_s() - t0) * 1e9 / (double)hops;
	__asm__ volatile("" : : "r"(p), "r"(c[n / 2]));
	struct stats st = samples_stats(&bw);
	printf("memory size=%zu MiB: triad median %.2f GB/s, random-access latency %.1f ns\n",
	       o->size >> 20, st.p50, chase_ns);
	json_begin(o);
	json_num("size", (double)o->size);
	json_stats("triad_gbps", &st);
	json_num("pointer_chase_ns", chase_ns);
	json_end();
	free(a); free(b); free(c); free(next);
	return 0;
}

/* ---------- network (loopback TCP) ---------- */

struct srv {
	int lfd;
	size_t msg;
	int mode_lat;
};

static void *server_thread(void *p)
{
	struct srv *s = p;
	int fd = accept(s->lfd, NULL, NULL);
	if (fd < 0)
		return NULL;
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	char *buf = malloc(1 << 17);
	for (;;) {
		ssize_t n = read(fd, buf, s->mode_lat ? s->msg : (1 << 17));
		if (n <= 0)
			break;
		if (s->mode_lat) {
			size_t got = (size_t)n;
			while (got < s->msg) {
				ssize_t k = read(fd, buf + got, s->msg - got);
				if (k <= 0)
					goto out;
				got += (size_t)k;
			}
			if (write(fd, buf, s->msg) < 0)
				break;
		}
	}
out:
	free(buf);
	close(fd);
	return NULL;
}

static int bench_net(struct opts *o)
{
	int lat = !o->mode || !strcmp(o->mode, "latency");
	struct srv s = { .msg = o->msg, .mode_lat = lat };
	s.lfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	socklen_t al = sizeof(a);
	if (bind(s.lfd, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s.lfd, 1) < 0 ||
	    getsockname(s.lfd, (struct sockaddr *)&a, &al) < 0) {
		perror("socket");
		return 1;
	}
	pthread_t th;
	pthread_create(&th, NULL, server_thread, &s);
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
		perror("connect");
		return 1;
	}
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	size_t bufsz = lat ? o->msg : (1 << 17);
	char *buf = calloc(1, bufsz);
	struct samples rtt;
	samples_init(&rtt, 1 << 20);
	double bytes = 0, msgs = 0;
	double t_end_warm = now_s() + o->warmup;
	double t0 = 0, end = 0;
	int measuring = o->warmup <= 0;
	if (measuring) {
		t0 = now_s();
		end = t0 + o->time;
	}
	for (;;) {
		double t = now_s();
		if (!measuring && t >= t_end_warm) {
			measuring = 1;
			t0 = t;
			end = t0 + o->time;
		}
		if (measuring && t >= end)
			break;
		if (lat) {
			double s0 = now_s();
			if (write(fd, buf, o->msg) != (ssize_t)o->msg)
				break;
			size_t got = 0;
			while (got < o->msg) {
				ssize_t k = read(fd, buf + got, o->msg - got);
				if (k <= 0)
					goto done;
				got += (size_t)k;
			}
			if (measuring) {
				samples_add(&rtt, (now_s() - s0) * 1e6);
				msgs++;
			}
		} else {
			ssize_t w = write(fd, buf, bufsz);
			if (w <= 0)
				break;
			if (measuring)
				bytes += (double)w;
		}
	}
done:;
	double secs = now_s() - t0;
	shutdown(fd, SHUT_WR);
	close(fd);
	pthread_join(th, NULL);
	close(s.lfd);
	json_begin(o);
	json_str("mode", lat ? "latency" : "throughput");
	json_num("duration_s", secs);
	if (lat) {
		struct stats st = samples_stats(&rtt);
		printf("net latency msg=%zu: %.0f round-trips/s, RTT us p50=%.1f p95=%.1f p99=%.1f\n",
		       o->msg, msgs / secs, st.p50, st.p95, st.p99);
		json_num("msg", (double)o->msg);
		json_num("rtt_per_s", msgs / secs);
		json_stats("rtt_us", &st);
	} else {
		printf("net throughput: %.1f MB/s\n", bytes / 1e6 / secs);
		json_num("mbps", bytes / 1e6 / secs);
	}
	json_end();
	free(buf);
	return 0;
}

/* ---------- workload transition (Graph 3) ---------- */

static int bench_transition(struct opts *o)
{
	if (!o->file)
		o->file = "/var/tmp/fuhrer-bench.dat";
	if (prepare_file(o->file, o->size) < 0) {
		perror("prepare");
		return 1;
	}
	if (!o->timeline)
		o->timeline = "transition.csv";
	timeline_open(o->timeline);
	double base = 0, pt = o->phase_time;
	printf("transition: idle -> cpu -> randread -> seqread -> idle, %.0fs each; timeline %s\n",
	       pt, o->timeline);

	for (int i = 0; i < (int)pt; i++) { /* idle */
		sleep(1);
		timeline_row(base + i + 1, "idle", 0, 0, 0);
	}
	base += pt;
	for (int i = 0; i < (int)pt; i++) {
		double r = cpu_run((int)sysconf(_SC_NPROCESSORS_ONLN), 1.0);
		timeline_row(base + i + 1, "cpu", r, 0, 0);
	}
	base += pt;

	struct opts so = *o;
	struct storage_run r = { 0 };
	samples_init(&r.lat, 16);
	so.pattern = "rand"; so.op = "read"; so.bs = 4096; so.backend = FU_BACKEND_AUTO;
	drop_caches();
	storage_loop(&so, pt, &r, "randread", base, 0);
	base += pt;
	so.pattern = "seq"; so.bs = 1 << 20; so.depth = o->depth < 8 ? o->depth : 8;
	drop_caches();
	storage_loop(&so, pt, &r, "seqread", base, 0);
	base += pt;
	for (int i = 0; i < (int)pt; i++) {
		sleep(1);
		timeline_row(base + i + 1, "idle", 0, 0, 0);
	}
	printf("done: backend switches seen by libfuhrer: %llu\n",
	       (unsigned long long)r.io.backend_switches);
	return 0;
}

/* ---------- main ---------- */

static void usage(void)
{
	fprintf(stderr,
		"usage: fuhrer-bench <storage|cpu|memory|net|transition> [options]\n"
		"  --pattern rand|seq  --op read|write  --bs 4k  --size 256M  --depth 32\n"
		"  --backend auto|normal|batched|specialized  --file PATH  --drop-cache\n"
		"  --threads N  --mode latency|throughput  --msg 64  --phase-time 15\n"
		"  --time S  --warmup S  --json PATH|-  --timeline PATH\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage();
		return 2;
	}
	struct opts o = {
		.cmd = argv[1], .pattern = "rand", .op = "read", .bs = 4096,
		.size = 256u << 20, .msg = 64, .time = 10, .warmup = 2, .phase_time = 15,
		.depth = 32, .threads = 0, .backend = FU_BACKEND_AUTO,
	};
	if (!strcmp(o.cmd, "memory"))
		o.size = 64u << 20;
	static const struct option lo[] = {
		{ "pattern", 1, 0, 'p' }, { "op", 1, 0, 'o' }, { "bs", 1, 0, 'b' },
		{ "size", 1, 0, 's' }, { "time", 1, 0, 't' }, { "depth", 1, 0, 'd' },
		{ "backend", 1, 0, 'B' }, { "file", 1, 0, 'f' }, { "json", 1, 0, 'j' },
		{ "timeline", 1, 0, 'T' }, { "warmup", 1, 0, 'w' }, { "threads", 1, 0, 'n' },
		{ "mode", 1, 0, 'm' }, { "msg", 1, 0, 'M' }, { "phase-time", 1, 0, 'P' },
		{ "drop-cache", 0, 0, 'C' }, { "help", 0, 0, 'h' }, { 0, 0, 0, 0 },
	};
	optind = 2;
	int c;
	while ((c = getopt_long(argc, argv, "h", lo, NULL)) != -1) {
		switch (c) {
		case 'p': o.pattern = optarg; break;
		case 'o': o.op = optarg; break;
		case 'b': o.bs = parse_size(optarg); break;
		case 's': o.size = parse_size(optarg); break;
		case 't': o.time = atof(optarg); break;
		case 'd': o.depth = atoi(optarg); break;
		case 'B':
			o.backend = fu_backend_from_name(optarg);
			if (o.backend < 0) {
				fprintf(stderr, "unknown backend %s\n", optarg);
				return 2;
			}
			break;
		case 'f': o.file = optarg; break;
		case 'j': o.json = optarg; break;
		case 'T': o.timeline = optarg; break;
		case 'w': o.warmup = atof(optarg); break;
		case 'n': o.threads = atoi(optarg); break;
		case 'm': o.mode = optarg; break;
		case 'M': o.msg = parse_size(optarg); break;
		case 'P': o.phase_time = atof(optarg); break;
		case 'C': o.drop_cache = 1; break;
		default: usage(); return c == 'h' ? 0 : 2;
		}
	}
	if (o.bs == 0 || o.depth < 1 || o.time <= 0) {
		usage();
		return 2;
	}
	if (!strcmp(o.cmd, "storage")) return bench_storage(&o);
	if (!strcmp(o.cmd, "cpu")) return bench_cpu(&o);
	if (!strcmp(o.cmd, "memory")) return bench_memory(&o);
	if (!strcmp(o.cmd, "net")) return bench_net(&o);
	if (!strcmp(o.cmd, "transition")) return bench_transition(&o);
	usage();
	return 2;
}
