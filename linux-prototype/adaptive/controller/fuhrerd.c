/*
 * fuhrerd — FuhrerOS adaptive controller daemon.
 *
 *   profiler -> features -> classifier -> engine -> policy switch
 *
 * Outputs (all under run_dir / log_dir):
 *   status          key=value snapshot for `fuhrer status`
 *   state.shm       seqlocked policy record for libfuhrer
 *   adapt.log       every policy decision ([ADAPT] records)
 *   metrics.csv     one row per sample (features, class, policy)
 *
 * Signals: SIGTERM/SIGINT restore stock settings and exit; SIGHUP reloads
 * the config file; SIGUSR1 applies run_dir/mode.req (written by `fuhrer`).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fuhrer/common.h"
#include "fuhrer/config.h"
#include "fuhrer/engine.h"
#include "fuhrer/features.h"
#include "fuhrer/policy.h"
#include "fuhrer/profiler.h"
#include "fuhrer/shm.h"

static volatile sig_atomic_t g_stop, g_reload, g_modereq;

static void on_signal(int sig)
{
	if (sig == SIGHUP)
		g_reload = 1;
	else if (sig == SIGUSR1)
		g_modereq = 1;
	else
		g_stop = 1;
}

struct daemon_state {
	struct fu_config cfg;
	char cfg_path[256];
	char cmdline_mode[64];
	struct fu_policy_ctx pctx;
	struct fu_engine engine;
	struct fu_profiler *prof;
	struct fu_shared_state *shm;
	FILE *adapt_log;
	FILE *metrics;
	double started;
	double policy_since;
	double overhead_pct;
	double last_switch_ms;
	enum fu_class cls;
	const char *reason;
	struct fu_features feat;
	int have_feat;
	unsigned long samples;
};

static void path_join(char *out, size_t len, const char *dir, const char *name)
{
	snprintf(out, len, "%s/%s", dir, name);
}

static void mkdirs(const char *dir)
{
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s", dir);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

static struct fu_shared_state *shm_open_state(const char *run_dir)
{
	char path[256];
	path_join(path, sizeof(path), run_dir, "state.shm");
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (fd < 0)
		return NULL;
	if (ftruncate(fd, sizeof(struct fu_shared_state)) < 0) {
		close(fd);
		return NULL;
	}
	void *m = mmap(NULL, sizeof(struct fu_shared_state), PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, 0);
	close(fd);
	if (m == MAP_FAILED)
		return NULL;
	struct fu_shared_state *s = m;
	s->magic = FU_SHM_MAGIC;
	s->version = FU_SHM_VERSION;
	return s;
}

static void shm_publish(struct daemon_state *d)
{
	struct fu_shared_state *s = d->shm;
	if (!s)
		return;
	__atomic_add_fetch(&s->seq, 1, __ATOMIC_ACQ_REL); /* odd: writing */
	s->policy = d->engine.current;
	s->cls = d->cls;
	s->mode = d->cfg.mode;
	s->switches = d->engine.switches;
	s->policy_since = d->policy_since;
	__atomic_add_fetch(&s->seq, 1, __ATOMIC_ACQ_REL); /* even: stable */
}

static void write_status(struct daemon_state *d)
{
	char path[256], buf[4096];
	const struct fu_features *f = &d->feat;
	int active = d->cfg.mode != FU_MODE_OFF;
	struct fu_policy_metrics pm;
	fu_policy_get(d->engine.current)->collect_metrics(&d->pctx, &pm);
	int n = snprintf(buf, sizeof(buf),
		"version=%s\npid=%d\nuptime_s=%.1f\nmode=%s\nstatic_policy=%s\n"
		"runtime=ACTIVE\nprofiler=%s\nengine=%s\n"
		"interval_ms=%d\nhysteresis=%d\nsamples=%lu\n"
		"policy=%s\npolicy_since_s=%.2f\nswitches=%lu\nlast_switch_ms=%.3f\n"
		"knobs_total=%d\nknobs_applied=%d\nknobs_failed=%d\n"
		"class=%s\nreason=%s\n"
		"cpu_pct=%.1f\niowait_pct=%.1f\nmem_used_pct=%.1f\nctxsw_per_s=%.0f\n"
		"read_mbps=%.2f\nwrite_mbps=%.2f\niops=%.0f\nread_iops=%.0f\nwrite_iops=%.0f\n"
		"avg_io_kb=%.1f\nread_ratio=%.2f\nmerge_ratio=%.2f\nqueue_depth=%.2f\n"
		"disk_util_pct=%.1f\nrx_mbps=%.3f\ntx_mbps=%.3f\nrx_pps=%.0f\ntx_pps=%.0f\n"
		"io_syscalls_per_s=%.0f\ntop_cpu_pid=%d\ntop_cpu_comm=%s\ntop_cpu_pct=%.1f\n"
		"top_io_pid=%d\ntop_io_comm=%s\ntop_io_ops=%.0f\noverhead_pct=%.3f\n",
		FUHRER_VERSION, (int)getpid(), fu_now() - d->started, fu_mode_name(d->cfg.mode),
		fu_policy_name(d->cfg.static_policy),
		active ? "ACTIVE" : "INACTIVE",
		d->cfg.mode == FU_MODE_ADAPTIVE ? "ACTIVE" :
		d->cfg.mode == FU_MODE_STATIC ? "STATIC" : "PASSIVE",
		d->cfg.interval_ms, d->cfg.hysteresis, d->samples,
		fu_policy_name(d->engine.current), fu_now() - d->policy_since,
		d->engine.switches, d->last_switch_ms,
		pm.knobs_total, pm.knobs_applied, pm.knobs_failed,
		fu_class_name(d->cls), d->reason ? d->reason : "-",
		f->cpu_pct, f->iowait_pct, f->mem_used_pct, f->ctxsw_per_s,
		f->read_mbps, f->write_mbps, f->iops, f->read_iops, f->write_iops,
		f->avg_io_kb, f->read_ratio, f->merge_ratio, f->queue_depth,
		f->disk_util_pct, f->rx_mbps, f->tx_mbps, f->rx_pps, f->tx_pps,
		f->io_syscalls_per_s, (int)f->top_cpu.pid,
		f->top_cpu.comm[0] ? f->top_cpu.comm : "-", f->top_cpu.value,
		(int)f->top_io.pid, f->top_io.comm[0] ? f->top_io.comm : "-", f->top_io.value,
		d->overhead_pct);
	path_join(path, sizeof(path), d->cfg.run_dir, "status");
	fu_write_atomic(path, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
}

static void open_logs(struct daemon_state *d)
{
	char path[256];
	mkdirs(d->cfg.log_dir);
	path_join(path, sizeof(path), d->cfg.log_dir, "adapt.log");
	d->adapt_log = fopen(path, "a");
	path_join(path, sizeof(path), d->cfg.log_dir, "metrics.csv");
	struct stat st;
	/* Rotate once at 32 MiB; the previous file is kept as metrics.csv.1. */
	if (stat(path, &st) == 0 && st.st_size > (32 << 20)) {
		char old[270];
		snprintf(old, sizeof(old), "%s.1", path);
		rename(path, old);
	}
	int fresh = stat(path, &st) != 0 || st.st_size == 0;
	d->metrics = fopen(path, "a");
	if (d->metrics && fresh)
		fprintf(d->metrics,
			"wallclock,mode,class,reason,policy,cpu_pct,iowait_pct,mem_used_pct,"
			"ctxsw_per_s,run_queue,blocked,read_iops,write_iops,read_mbps,write_mbps,"
			"avg_io_kb,read_ratio,merge_ratio,queue_depth,disk_util_pct,rx_mbps,"
			"tx_mbps,rx_pps,tx_pps,io_syscalls_per_s,app_io_mbps,majfault_per_s,"
			"top_cpu_pid,top_cpu_comm,top_cpu_pct,top_io_pid,top_io_comm,top_io_ops,"
			"overhead_pct\n");
	if (d->adapt_log)
		setvbuf(d->adapt_log, NULL, _IOLBF, 0);
}

static void log_metrics(struct daemon_state *d)
{
	if (!d->metrics)
		return;
	const struct fu_features *f = &d->feat;
	fprintf(d->metrics,
		"%.3f,%s,%s,%s,%s,%.2f,%.2f,%.2f,%.0f,%.0f,%.0f,%.1f,%.1f,%.3f,%.3f,%.2f,%.3f,"
		"%.3f,%.3f,%.1f,%.4f,%.4f,%.0f,%.0f,%.0f,%.3f,%.1f,%d,%s,%.1f,%d,%s,%.0f,%.4f\n",
		fu_wallclock(), fu_mode_name(d->cfg.mode), fu_class_name(d->cls),
		d->reason ? d->reason : "-", fu_policy_name(d->engine.current), f->cpu_pct,
		f->iowait_pct, f->mem_used_pct, f->ctxsw_per_s, f->run_queue, f->blocked,
		f->read_iops, f->write_iops, f->read_mbps, f->write_mbps, f->avg_io_kb,
		f->read_ratio, f->merge_ratio, f->queue_depth, f->disk_util_pct, f->rx_mbps,
		f->tx_mbps, f->rx_pps, f->tx_pps, f->io_syscalls_per_s, f->app_io_mbps,
		f->majfault_per_s, (int)f->top_cpu.pid,
		f->top_cpu.comm[0] ? f->top_cpu.comm : "-", f->top_cpu.value, (int)f->top_io.pid,
		f->top_io.comm[0] ? f->top_io.comm : "-", f->top_io.value, d->overhead_pct);
	fflush(d->metrics);
}

static void log_adapt(struct daemon_state *d, enum fu_policy_id from, enum fu_policy_id to,
		      const char *reason, double switch_ms, int ok)
{
	const struct fu_features *f = &d->feat;
	int io_side = d->cls == FU_CLASS_IO_RANDOM || d->cls == FU_CLASS_IO_SEQUENTIAL;
	const struct fu_proc_top *top = io_side && f->top_io.pid ? &f->top_io : &f->top_cpu;
	char ts[32];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm);
	char rec[1024];
	snprintf(rec, sizeof(rec),
		 "[ADAPT] time=%s mode=%s\n"
		 "PID=%d COMM=%s\n"
		 "IOPS=%.0f avg_io=%.0f CPU=%.0f%% queue_depth=%.1f rx_pps=%.0f tx_pps=%.0f\n"
		 "class=%s\n"
		 "policy: %s -> %s\n"
		 "old=%s new=%s\n"
		 "reason=%s\n"
		 "switch_ms=%.3f status=%s\n",
		 ts, fu_mode_name(d->cfg.mode), (int)top->pid, top->comm[0] ? top->comm : "-",
		 f->iops, f->avg_io_kb * 1024.0, f->cpu_pct, f->queue_depth, f->rx_pps,
		 f->tx_pps, fu_class_name(d->cls), fu_policy_name(from), fu_policy_name(to),
		 fu_policy_name(from), fu_policy_name(to), reason, switch_ms,
		 ok ? "ok" : "partial");
	if (d->adapt_log)
		fprintf(d->adapt_log, "%s\n", rec);
	fprintf(stderr, "%s", rec);
}

static void do_switch(struct daemon_state *d, enum fu_policy_id to, const char *reason)
{
	enum fu_policy_id from = d->engine.current;
	double t0 = fu_now();
	const struct fu_policy *old = fu_policy_get(from), *nw = fu_policy_get(to);
	int ok = old->deactivate(&d->pctx) == 0;
	ok &= nw->activate(&d->pctx) == 0;
	double ms = (fu_now() - t0) * 1000.0;
	d->last_switch_ms = ms;
	fu_engine_commit(&d->engine, to, fu_now());
	d->policy_since = fu_now();
	log_adapt(d, from, to, reason, ms, ok);
	shm_publish(d);
}

static void read_cmdline_mode(struct daemon_state *d)
{
	char buf[4096];
	d->cmdline_mode[0] = '\0';
	if (fu_read_file_abs("/proc/cmdline", buf, sizeof(buf)) <= 0)
		return;
	char *p = strstr(buf, "fuhrer.mode=");
	if (!p)
		return;
	p += 12;
	size_t n = strcspn(p, " \n");
	if (n >= sizeof(d->cmdline_mode))
		n = sizeof(d->cmdline_mode) - 1;
	memcpy(d->cmdline_mode, p, n);
	d->cmdline_mode[n] = '\0';
}

static void load_config(struct daemon_state *d, const char *mode_override)
{
	fu_config_default(&d->cfg);
	int errs = fu_config_load(&d->cfg, d->cfg_path);
	if (errs)
		FU_WARN("%d error(s) in %s; defaults used for those keys", errs, d->cfg_path);
	if (d->cmdline_mode[0] && fu_config_set_mode(&d->cfg, d->cmdline_mode) < 0)
		FU_WARN("ignoring bad kernel parameter fuhrer.mode=%s", d->cmdline_mode);
	if (mode_override && fu_config_set_mode(&d->cfg, mode_override) < 0)
		FU_WARN("ignoring bad --mode %s", mode_override);
}

static void apply_mode_request(struct daemon_state *d)
{
	char path[256], buf[128];
	path_join(path, sizeof(path), d->cfg.run_dir, "mode.req");
	if (fu_read_file_abs(path, buf, sizeof(buf)) <= 0)
		return;
	fu_rtrim(buf);
	enum fu_mode before = d->cfg.mode;
	if (fu_config_set_mode(&d->cfg, buf) < 0) {
		FU_WARN("bad mode request '%s'", buf);
		return;
	}
	unlink(path);
	FU_INFO("mode %s -> %s (%s)", fu_mode_name(before), fu_mode_name(d->cfg.mode), buf);
	if (d->adapt_log)
		fprintf(d->adapt_log, "[MODE] %s -> %s\n\n", fu_mode_name(before), buf);
	/* Leaving the engine's hands: reset streaks so the new mode acts now. */
	d->engine.streak = 0;
}

static double cpu_seconds_self(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return (double)ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 +
	       (double)ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
}

static void sleep_interval(int ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
	while (!g_stop && !g_modereq && nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: fuhrerd [-c config] [-m mode] [-n samples] [-r run_dir] [-l log_dir]\n"
		"               [--dry-run] [--print-config]\n"
		"modes: adaptive | observe | off | static:{normal,batched,specialized}\n");
}

int main(int argc, char **argv)
{
	static struct daemon_state d;
	const char *mode_override = NULL, *run_dir = NULL, *log_dir = NULL;
	long max_samples = -1;
	int dry_run = 0, print_config = 0;
	snprintf(d.cfg_path, sizeof(d.cfg_path), "/etc/fuhrer/fuhrer.conf");

	static const struct option opts[] = {
		{ "config", required_argument, 0, 'c' },
		{ "mode", required_argument, 0, 'm' },
		{ "samples", required_argument, 0, 'n' },
		{ "run-dir", required_argument, 0, 'r' },
		{ "log-dir", required_argument, 0, 'l' },
		{ "dry-run", no_argument, 0, 'D' },
		{ "print-config", no_argument, 0, 'P' },
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 },
	};
	int c;
	while ((c = getopt_long(argc, argv, "c:m:n:r:l:h", opts, NULL)) != -1) {
		switch (c) {
		case 'c': snprintf(d.cfg_path, sizeof(d.cfg_path), "%s", optarg); break;
		case 'm': mode_override = optarg; break;
		case 'n': max_samples = atol(optarg); break;
		case 'r': run_dir = optarg; break;
		case 'l': log_dir = optarg; break;
		case 'D': dry_run = 1; break;
		case 'P': print_config = 1; break;
		default: usage(); return c == 'h' ? 0 : 2;
		}
	}

	read_cmdline_mode(&d);
	load_config(&d, mode_override);
	if (run_dir) snprintf(d.cfg.run_dir, sizeof(d.cfg.run_dir), "%s", run_dir);
	if (log_dir) snprintf(d.cfg.log_dir, sizeof(d.cfg.log_dir), "%s", log_dir);
	if (dry_run) d.cfg.dry_run = 1;
	if (print_config) {
		fu_config_dump(&d.cfg, stdout);
		return 0;
	}

	struct sigaction sa = { .sa_handler = on_signal };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);

	mkdirs(d.cfg.run_dir);
	char path[256], pid[32];
	path_join(path, sizeof(path), d.cfg.run_dir, "fuhrerd.pid");
	int pn = snprintf(pid, sizeof(pid), "%d\n", (int)getpid());
	fu_write_atomic(path, pid, (size_t)pn);

	path_join(path, sizeof(path), d.cfg.run_dir, "knobs.orig");
	int nk = fu_knobs_init(&d.pctx, path, d.cfg.dry_run);
	/* Undo anything a crashed predecessor left behind. */
	fu_knobs_restore_all(&d.pctx);
	for (int i = 0; i < FU_POLICY_COUNT; i++)
		fu_policy_get(i)->initialize(&d.pctx);

	open_logs(&d);
	d.shm = shm_open_state(d.cfg.run_dir);
	d.prof = fu_profiler_new(d.cfg.scan_procs);
	if (!d.prof) {
		FU_ERR("out of memory");
		return 1;
	}
	d.started = d.policy_since = fu_now();
	fu_engine_init(&d.engine, &d.cfg, FU_POLICY_NORMAL, d.started);
	d.cls = FU_CLASS_IDLE;
	int ncpus = (int)sysconf(_SC_NPROCESSORS_ONLN);

	FU_INFO("fuhrerd %s starting: mode=%s interval=%dms hysteresis=%d knobs=%d%s",
		FUHRER_VERSION, fu_mode_name(d.cfg.mode), d.cfg.interval_ms, d.cfg.hysteresis,
		nk, d.cfg.dry_run ? " (dry-run)" : "");
	if (d.adapt_log)
		fprintf(d.adapt_log, "[START] fuhrerd %s mode=%s interval_ms=%d hysteresis=%d\n\n",
			FUHRER_VERSION, fu_mode_name(d.cfg.mode), d.cfg.interval_ms,
			d.cfg.hysteresis);
	shm_publish(&d);

	struct fu_sample prev, cur;
	int have_prev = 0;
	double cpu0 = cpu_seconds_self(), wall0 = fu_now();

	while (!g_stop && (max_samples < 0 || (long)d.samples < max_samples)) {
		if (g_reload) {
			g_reload = 0;
			enum fu_mode m = d.cfg.mode;
			enum fu_policy_id sp = d.cfg.static_policy;
			load_config(&d, mode_override);
			/* A runtime mode chosen via `fuhrer policy` survives reloads. */
			d.cfg.mode = m;
			d.cfg.static_policy = sp;
			FU_INFO("configuration reloaded");
		}
		if (g_modereq) {
			g_modereq = 0;
			apply_mode_request(&d);
		}

		if (d.cfg.mode == FU_MODE_OFF) {
			/* A1/B0: no profiling at all, stock policy. */
			if (d.engine.current != FU_POLICY_NORMAL)
				do_switch(&d, FU_POLICY_NORMAL, "mode_off");
			have_prev = 0;
			d.have_feat = 0;
			memset(&d.feat, 0, sizeof(d.feat));
			write_status(&d);
			sleep_interval(d.cfg.interval_ms);
			continue;
		}

		if (fu_profiler_sample(d.prof, &cur) < 0) {
			FU_ERR("profiler: cannot read /proc/stat");
			sleep_interval(d.cfg.interval_ms);
			continue;
		}
		if (have_prev) {
			fu_features_compute(&prev, &cur, ncpus, &d.feat);
			d.have_feat = 1;
			d.cls = fu_classify(&d.feat, &d.cfg.th, &d.reason);
			d.samples++;

			struct fu_decision dec;
			fu_engine_step(&d.engine, d.cls, fu_now(), &dec);
			if (dec.do_switch) {
				const char *why = d.cfg.mode == FU_MODE_ADAPTIVE ? d.reason
						 : d.cfg.mode == FU_MODE_STATIC ? "static_mode"
						 : "observe_mode";
				do_switch(&d, dec.to, why);
			}

			double wall = fu_now(), cpu = cpu_seconds_self();
			if (wall - wall0 > 0)
				d.overhead_pct = 100.0 * (cpu - cpu0) / (wall - wall0) /
						 (ncpus > 0 ? ncpus : 1);
			cpu0 = cpu;
			wall0 = wall;
			log_metrics(&d);
			shm_publish(&d);
			write_status(&d);
		}
		prev = cur;
		have_prev = 1;
		sleep_interval(d.cfg.interval_ms);
	}

	FU_INFO("fuhrerd stopping; restoring stock settings");
	fu_knobs_restore_all(&d.pctx);
	d.engine.current = FU_POLICY_NORMAL;
	if (d.adapt_log)
		fprintf(d.adapt_log, "[STOP] restored stock settings, switches=%lu\n\n",
			d.engine.switches);
	shm_publish(&d);
	path_join(path, sizeof(path), d.cfg.run_dir, "status");
	unlink(path);
	path_join(path, sizeof(path), d.cfg.run_dir, "fuhrerd.pid");
	unlink(path);
	fu_profiler_free(d.prof);
	return 0;
}
