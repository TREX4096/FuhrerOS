/*
 * fuhrer — FuhrerOS command-line front end (M1 runtime + research dashboard).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fuhrer/common.h"
#include "fuhrer/config.h"
#include "fuhrer/features.h"
#include "fuhrer/policy.h"
#include "fuhrer/profiler.h"

static const char *run_dir = "/run/fuhrer";
static const char *log_dir = "/var/log/fuhrer";

#define MAX_KV 96
struct kv {
	char k[48];
	char v[96];
};
static struct kv status_kv[MAX_KV];
static int status_n;

static int load_status(void)
{
	char path[256], buf[8192];
	snprintf(path, sizeof(path), "%s/status", run_dir);
	status_n = 0;
	if (fu_read_file_abs(path, buf, sizeof(buf)) <= 0)
		return -1;
	char *save = NULL;
	for (char *l = strtok_r(buf, "\n", &save); l && status_n < MAX_KV;
	     l = strtok_r(NULL, "\n", &save)) {
		char *eq = strchr(l, '=');
		if (!eq)
			continue;
		*eq = '\0';
		snprintf(status_kv[status_n].k, sizeof(status_kv[0].k), "%s", l);
		snprintf(status_kv[status_n].v, sizeof(status_kv[0].v), "%s", eq + 1);
		status_n++;
	}
	return 0;
}

static const char *S(const char *key)
{
	for (int i = 0; i < status_n; i++)
		if (!strcmp(status_kv[i].k, key))
			return status_kv[i].v;
	return "?";
}

static double D(const char *key)
{
	return atof(S(key));
}

static int daemon_pid(void)
{
	char path[256], buf[32];
	snprintf(path, sizeof(path), "%s/fuhrerd.pid", run_dir);
	if (fu_read_file_abs(path, buf, sizeof(buf)) <= 0)
		return -1;
	int pid = atoi(buf);
	/* EPERM means the (root) daemon exists but we may not signal it. */
	if (pid <= 0 || (kill(pid, 0) < 0 && errno != EPERM))
		return -1;
	return pid;
}

static const char *bar(double pct)
{
	static char b[24];
	int n = (int)(pct / 5.0 + 0.5);
	if (n < 0) n = 0;
	if (n > 20) n = 20;
	for (int i = 0; i < 20; i++)
		b[i] = i < n ? '#' : '.';
	b[20] = '\0';
	return b;
}

static int cmd_status(int argc, char **argv)
{
	int raw = argc > 1 && !strcmp(argv[1], "--raw");
	int pid = daemon_pid();
	if (pid < 0 || load_status() < 0) {
		printf("FUHREROS STATUS\n────────────────────────\n\n"
		       "Runtime:        INACTIVE (fuhrerd not running)\n"
		       "Hint:           rc-service fuhrerd start\n");
		return 1;
	}
	if (raw) {
		for (int i = 0; i < status_n; i++)
			printf("%s=%s\n", status_kv[i].k, status_kv[i].v);
		return 0;
	}
	const char *mode = S("mode");
	printf("FUHREROS STATUS\n────────────────────────\n\n");
	printf("Runtime:        %s (fuhrerd pid %d, v%s)\n", S("runtime"), pid, S("version"));
	printf("Profiler:       %s\n", S("profiler"));
	printf("Policy engine:  %s\n", S("engine"));
	printf("Mode:           %s%s%s\n\n", mode, !strcmp(mode, "static") ? ":" : "",
	       !strcmp(mode, "static") ? S("static_policy") : "");
	printf("CPU:            %3.0f%%  [%s]\n", D("cpu_pct"), bar(D("cpu_pct")));
	printf("Memory:         %3.0f%%  [%s]\n\n", D("mem_used_pct"), bar(D("mem_used_pct")));
	printf("I/O:\n");
	printf("  Read:         %.1f MB/s\n", D("read_mbps"));
	printf("  Write:        %.1f MB/s\n", D("write_mbps"));
	printf("  IOPS:         %.0f\n", D("iops"));
	printf("  Avg request:  %.1f KB\n", D("avg_io_kb"));
	printf("  Queue depth:  %.2f\n\n", D("queue_depth"));
	printf("Network:\n");
	printf("  RX:           %.2f MB/s (%.0f pkt/s)\n", D("rx_mbps"), D("rx_pps"));
	printf("  TX:           %.2f MB/s (%.0f pkt/s)\n\n", D("tx_mbps"), D("tx_pps"));
	printf("Workload:\n  %s (%s)\n\n", S("class"), S("reason"));
	printf("Policy:\n  %s\n\n", S("policy"));
	printf("Policy since:\n  %.2f seconds\n\n", D("policy_since_s"));
	printf("Switches:\n  %s (last switch %.3f ms)\n\n", S("switches"), D("last_switch_ms"));
	printf("Top process:    %s[%s] %.0f%% CPU\n", S("top_cpu_comm"), S("top_cpu_pid"),
	       D("top_cpu_pct"));
	printf("Top I/O:        %s[%s] %.0f ops/s\n", S("top_io_comm"), S("top_io_pid"),
	       D("top_io_ops"));
	printf("Samples:        %s @ %s ms   Daemon overhead: %.3f%% CPU\n", S("samples"),
	       S("interval_ms"), D("overhead_pct"));
	return 0;
}

static int cmd_profile(int argc, char **argv)
{
	int interval = 1000, count = 10;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-i") && i + 1 < argc)
			interval = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-n") && i + 1 < argc)
			count = atoi(argv[++i]);
		else {
			fprintf(stderr, "usage: fuhrer profile [-i interval_ms] [-n samples]\n");
			return 2;
		}
	}
	if (interval < 10)
		interval = 10;
	struct fu_profiler *p = fu_profiler_new(1);
	struct fu_thresholds th;
	fu_thresholds_default(&th);
	/* Honour thresholds from the daemon config so both agree. */
	struct fu_config cfg;
	fu_config_default(&cfg);
	fu_config_load(&cfg, "/etc/fuhrer/fuhrer.conf");
	th = cfg.th;
	int ncpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
	struct fu_sample a, b;
	struct fu_features f;
	fu_profiler_sample(p, &a);
	printf("%5s %6s %6s %8s %7s %7s %7s %6s %6s %8s %8s  %-14s %s\n", "t", "cpu%", "mem%",
	       "ctxsw/s", "iops", "rdMB/s", "wrMB/s", "avgKB", "qd", "rxMB/s", "txMB/s",
	       "class", "top");
	for (int i = 0; i < count; i++) {
		usleep((useconds_t)interval * 1000);
		fu_profiler_sample(p, &b);
		fu_features_compute(&a, &b, ncpus, &f);
		const char *why;
		enum fu_class c = fu_classify(&f, &th, &why);
		printf("%5.1f %6.1f %6.1f %8.0f %7.0f %7.2f %7.2f %6.1f %6.2f %8.3f %8.3f  %-14s "
		       "%s[%d]\n",
		       (i + 1) * interval / 1000.0, f.cpu_pct, f.mem_used_pct, f.ctxsw_per_s,
		       f.iops, f.read_mbps, f.write_mbps, f.avg_io_kb, f.queue_depth, f.rx_mbps,
		       f.tx_mbps, fu_class_name(c), f.top_cpu.comm[0] ? f.top_cpu.comm : "-",
		       (int)f.top_cpu.pid);
		fflush(stdout);
		a = b;
	}
	fu_profiler_free(p);
	return 0;
}

static int request_mode(const char *spec)
{
	struct fu_config tmp;
	fu_config_default(&tmp);
	if (fu_config_set_mode(&tmp, spec) < 0) {
		fprintf(stderr, "fuhrer: unknown policy/mode '%s'\n", spec);
		return 2;
	}
	int pid = daemon_pid();
	if (pid < 0) {
		fprintf(stderr, "fuhrer: fuhrerd is not running\n");
		return 1;
	}
	char path[256];
	snprintf(path, sizeof(path), "%s/mode.req", run_dir);
	if (fu_write_atomic(path, spec, strlen(spec)) < 0 || kill(pid, SIGUSR1) < 0) {
		fprintf(stderr, "fuhrer: cannot signal fuhrerd: %s (are you root?)\n",
			strerror(errno));
		return 1;
	}
	/* Wait briefly for the daemon to pick it up so the reply is accurate. */
	for (int i = 0; i < 50; i++) {
		usleep(20000);
		if (access(path, F_OK) != 0)
			break;
	}
	usleep(50000);
	load_status();
	printf("mode: %s  policy: %s\n", S("mode"), S("policy"));
	return 0;
}

static int cmd_policy(int argc, char **argv)
{
	if (argc < 2) {
		if (daemon_pid() < 0 || load_status() < 0) {
			printf("policy: NORMAL (fuhrerd not running; stock Linux settings)\n");
			return 1;
		}
		printf("mode:    %s%s%s\npolicy:  %s\nclass:   %s (%s)\nswitches: %s\n", S("mode"),
		       !strcmp(S("mode"), "static") ? ":" : "",
		       !strcmp(S("mode"), "static") ? S("static_policy") : "", S("policy"),
		       S("class"), S("reason"), S("switches"));
		printf("\navailable: auto | observe | off | normal | batched | specialized\n");
		return 0;
	}
	const char *a = argv[1];
	if (!strcmp(a, "set") && argc > 2)
		a = argv[2];
	char spec[64];
	if (!strcasecmp(a, "auto") || !strcasecmp(a, "adaptive"))
		snprintf(spec, sizeof(spec), "adaptive");
	else if (!strcasecmp(a, "observe") || !strcasecmp(a, "off"))
		snprintf(spec, sizeof(spec), "%s", a);
	else if (!strncasecmp(a, "static:", 7))
		snprintf(spec, sizeof(spec), "%s", a);
	else
		snprintf(spec, sizeof(spec), "static:%s", a);
	return request_mode(spec);
}

static int cmd_classify(void)
{
	if (daemon_pid() < 0 || load_status() < 0) {
		fprintf(stderr, "fuhrerd not running; use 'fuhrer profile' for a live sample\n");
		return 1;
	}
	printf("class=%s reason=%s policy=%s\n", S("class"), S("reason"), S("policy"));
	printf("cpu=%.1f%% iops=%.0f avg_io_kb=%.1f merge_ratio=%.2f qd=%.2f "
	       "rx_pps=%.0f tx_pps=%.0f io_syscalls/s=%.0f\n",
	       D("cpu_pct"), D("iops"), D("avg_io_kb"), D("merge_ratio"), D("queue_depth"),
	       D("rx_pps"), D("tx_pps"), D("io_syscalls_per_s"));
	return 0;
}

static int cmd_log(int argc, char **argv)
{
	int n = 40;
	if (argc > 2 && !strcmp(argv[1], "-n"))
		n = atoi(argv[2]);
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "tail -n %d %s/adapt.log 2>/dev/null", n, log_dir);
	return system(cmd) == 0 ? 0 : 1;
}

static int cmd_benchmark(int argc, char **argv)
{
	char **args = calloc((size_t)argc + 1, sizeof(char *));
	args[0] = "fuhrer-bench";
	for (int i = 1; i < argc; i++)
		args[i] = argv[i];
	execvp("fuhrer-bench", args);
	perror("fuhrer-bench");
	return 127;
}

static void help(void)
{
	printf("FuhrerOS runtime %s\n\n"
	       "  fuhrer status [--raw]        research dashboard\n"
	       "  fuhrer profile [-i ms] [-n N] live workload profile + classification\n"
	       "  fuhrer policy                show mode/policy\n"
	       "  fuhrer policy <p>            p = auto|observe|off|normal|batched|specialized\n"
	       "  fuhrer classify              current workload class and features\n"
	       "  fuhrer log [-n N]            recent [ADAPT] decisions\n"
	       "  fuhrer benchmark <args>      run fuhrer-bench (see fuhrer benchmark help)\n"
	       "  fuhrer version\n"
	       "  fuhrer                       interactive shell\n",
	       FUHRER_VERSION);
}

static int dispatch(int argc, char **argv)
{
	if (argc < 1)
		return 0;
	const char *c = argv[0];
	if (!strcmp(c, "status")) return cmd_status(argc, argv);
	if (!strcmp(c, "profile")) return cmd_profile(argc, argv);
	if (!strcmp(c, "policy")) return cmd_policy(argc, argv);
	if (!strcmp(c, "classify")) return cmd_classify();
	if (!strcmp(c, "log")) return cmd_log(argc, argv);
	if (!strcmp(c, "benchmark") || !strcmp(c, "bench")) return cmd_benchmark(argc, argv);
	if (!strcmp(c, "version")) { printf("FuhrerOS runtime %s\n", FUHRER_VERSION); return 0; }
	if (!strcmp(c, "help") || !strcmp(c, "-h") || !strcmp(c, "--help")) { help(); return 0; }
	fprintf(stderr, "fuhrer: unknown command '%s' (try 'help')\n", c);
	return 2;
}

static int repl(void)
{
	char line[512];
	printf("FuhrerOS runtime %s — type 'help', 'exit' to leave\n", FUHRER_VERSION);
	for (;;) {
		printf("fuhrer> ");
		fflush(stdout);
		if (!fgets(line, sizeof(line), stdin))
			break;
		char *argv[32];
		int argc = 0;
		char *save = NULL;
		for (char *t = strtok_r(line, " \t\n", &save); t && argc < 31;
		     t = strtok_r(NULL, " \t\n", &save))
			argv[argc++] = t;
		argv[argc] = NULL;
		if (!argc)
			continue;
		if (!strcmp(argv[0], "exit") || !strcmp(argv[0], "quit"))
			break;
		if (!strcmp(argv[0], "benchmark") || !strcmp(argv[0], "bench")) {
			/* Don't let exec replace the shell. */
			pid_t pid = fork();
			if (pid == 0)
				_exit(cmd_benchmark(argc, argv));
			int st;
			waitpid(pid, &st, 0);
			continue;
		}
		dispatch(argc, argv);
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *e;
	if ((e = getenv("FUHRER_RUN_DIR")))
		run_dir = e;
	if ((e = getenv("FUHRER_LOG_DIR")))
		log_dir = e;
	if (argc < 2)
		return repl();
	return dispatch(argc - 1, argv + 1);
}
