/*
 * Unit tests for the adaptive layer: parsers, features, classifier, engine,
 * config and the reversible knob manager (against a fake sysfs tree).
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fuhrer/common.h"
#include "fuhrer/config.h"
#include "fuhrer/engine.h"
#include "fuhrer/features.h"
#include "fuhrer/policy.h"
#include "fuhrer/profiler.h"

static int failures, checks;

#define CHECK(cond)                                                               \
	do {                                                                      \
		checks++;                                                         \
		if (!(cond)) {                                                    \
			failures++;                                               \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                 \
	} while (0)

#define NEAR(a, b) (((a) - (b)) < 1e-6 && ((b) - (a)) < 1e-6)

static void test_parsers(void)
{
	struct fu_sample s;
	memset(&s, 0, sizeof(s));
	char stat[] = "cpu  100 5 50 800 20 1 2 3 0 0\n"
		      "cpu0 50 2 25 400 10 0 1 1 0 0\n"
		      "intr 123\nctxt 98765\nbtime 1\nprocesses 50\n"
		      "procs_running 3\nprocs_blocked 1\n";
	CHECK(fu_parse_proc_stat(stat, &s) == 0);
	CHECK(s.cpu.user == 100 && s.cpu.idle == 800 && s.cpu.steal == 3);
	CHECK(s.ctxt == 98765 && s.procs_running == 3 && s.procs_blocked == 1);

	char mem[] = "MemTotal:        2048000 kB\nMemFree:  100 kB\nMemAvailable:    1024000 kB\n";
	CHECK(fu_parse_meminfo(mem, &s) == 0);
	CHECK(s.mem_total_kb == 2048000 && s.mem_avail_kb == 1024000);

	CHECK(fu_is_physical_disk("vda"));
	CHECK(!fu_is_physical_disk("vda1"));
	CHECK(fu_is_physical_disk("sdb"));
	CHECK(fu_is_physical_disk("nvme0n1"));
	CHECK(!fu_is_physical_disk("nvme0n1p2"));
	CHECK(!fu_is_physical_disk("loop0"));
	CHECK(!fu_is_physical_disk("dm-0"));

	char disk[] =
		" 253       0 vda 1000 10 80000 500 200 20 16000 300 2 900 1200 0 0 0 0\n"
		" 253       1 vda1 999 10 79000 499 200 20 16000 300 0 800 1100 0 0 0 0\n"
		"   7       0 loop0 5 0 10 0 0 0 0 0 0 0 0 0 0 0 0\n"
		" 253      16 vdb 1 2 3 4 5 6 7 8 0 9 10 0 0 0 0\n";
	struct fu_disk_counters d;
	CHECK(fu_parse_diskstats(disk, &d) == 2);
	CHECK(d.rd_ios == 1001 && d.rd_merges == 12 && d.wr_sectors == 16007);
	CHECK(d.in_flight == 2 && d.time_in_queue == 1210);

	char net[] =
		"Inter-|   Receive                                                |  Transmit\n"
		" face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
		"    lo: 5000 50 0 0 0 0 0 0 5000 50 0 0 0 0 0 0\n"
		"  eth0: 1000000 1000 0 0 0 0 0 0 200000 500 0 0 0 0 0 0\n";
	struct fu_net_counters n;
	CHECK(fu_parse_netdev(net, &n) == 1);
	CHECK(n.rx_bytes == 1000000 && n.tx_packets == 500);
}

static void mk_sample(struct fu_sample *s, double t)
{
	memset(s, 0, sizeof(*s));
	s->t = t;
	s->mem_total_kb = 1000;
	s->mem_avail_kb = 500;
}

static void test_features_and_classifier(void)
{
	struct fu_thresholds th;
	fu_thresholds_default(&th);
	struct fu_sample a, b;
	struct fu_features f;
	const char *why;

	/* Random 4 KiB reads: 1000 IOPS, 8 sectors each, no merges. */
	mk_sample(&a, 10.0);
	mk_sample(&b, 11.0);
	b.cpu.user = 10; b.cpu.idle = 90;
	b.disk.rd_ios = 1000; b.disk.rd_sectors = 8000; b.disk.time_in_queue = 4000;
	fu_features_compute(&a, &b, 2, &f);
	CHECK(NEAR(f.iops, 1000.0));
	CHECK(NEAR(f.avg_io_kb, 4.0));
	CHECK(NEAR(f.queue_depth, 4.0));
	CHECK(NEAR(f.cpu_pct, 10.0));
	CHECK(NEAR(f.mem_used_pct, 50.0));
	CHECK(fu_classify(&f, &th, &why) == FU_CLASS_IO_RANDOM);
	CHECK(!strcmp(why, "high_small_io"));

	/* Large sequential reads: 300 IOPS of 512 KiB. */
	mk_sample(&b, 11.0);
	b.disk.rd_ios = 300; b.disk.rd_sectors = 300 * 1024; b.disk.rd_merges = 900;
	fu_features_compute(&a, &b, 2, &f);
	CHECK(fu_classify(&f, &th, &why) == FU_CLASS_IO_SEQUENTIAL);

	/* Small but heavily merged requests are sequential too. */
	mk_sample(&b, 11.0);
	b.disk.rd_ios = 1000; b.disk.rd_sectors = 8000; b.disk.rd_merges = 3000;
	fu_features_compute(&a, &b, 2, &f);
	CHECK(fu_classify(&f, &th, &why) == FU_CLASS_IO_SEQUENTIAL);

	/* CPU bound. */
	mk_sample(&b, 11.0);
	b.cpu.user = 95; b.cpu.idle = 5;
	fu_features_compute(&a, &b, 2, &f);
	CHECK(fu_classify(&f, &th, &why) == FU_CLASS_CPU_BOUND);

	/* Network: 10k small packets/s. */
	mk_sample(&b, 11.0);
	b.net.rx_packets = 10000; b.net.rx_bytes = 10000 * 100;
	fu_features_compute(&a, &b, 2, &f);
	CHECK(fu_classify(&f, &th, &why) == FU_CLASS_NETWORK_HEAVY);
	CHECK(!strcmp(why, "small_packet_rate"));

	/* Disk + network at once -> MIXED. */
	b.disk.rd_ios = 1000; b.disk.rd_sectors = 8000;
	fu_features_compute(&a, &b, 2, &f);
	CHECK(fu_classify(&f, &th, &why) == FU_CLASS_MIXED);

	/* Idle. */
	mk_sample(&b, 11.0);
	b.cpu.idle = 100;
	fu_features_compute(&a, &b, 2, &f);
	CHECK(fu_classify(&f, &th, &why) == FU_CLASS_IDLE);

	/* Counter wrap/reset must not produce huge rates. */
	mk_sample(&a, 10.0);
	a.disk.rd_ios = 5000;
	mk_sample(&b, 11.0);
	b.disk.rd_ios = 10;
	fu_features_compute(&a, &b, 2, &f);
	CHECK(NEAR(f.read_iops, 0.0));

	CHECK(fu_class_from_name("io_random") == FU_CLASS_IO_RANDOM);
	CHECK(fu_class_from_name("bogus") == -1);
}

static void test_engine(void)
{
	struct fu_config c;
	fu_config_default(&c);
	c.hysteresis = 3;
	c.min_dwell_ms = 5000;
	struct fu_engine e;
	struct fu_decision d;
	fu_engine_init(&e, &c, FU_POLICY_NORMAL, 100.0);

	/* Two random samples are not enough, the third switches. */
	fu_engine_step(&e, FU_CLASS_IO_RANDOM, 101, &d);
	CHECK(!d.do_switch && !strcmp(d.why_not, "hysteresis"));
	fu_engine_step(&e, FU_CLASS_IO_RANDOM, 102, &d);
	CHECK(!d.do_switch);
	fu_engine_step(&e, FU_CLASS_IO_RANDOM, 103, &d);
	CHECK(d.do_switch && d.to == FU_POLICY_SPECIALIZED);
	fu_engine_commit(&e, d.to, 103);
	CHECK(e.switches == 1);

	/* A noisy interruption resets the streak. */
	fu_engine_step(&e, FU_CLASS_IO_SEQUENTIAL, 104, &d);
	fu_engine_step(&e, FU_CLASS_IO_RANDOM, 105, &d);
	CHECK(!d.do_switch);
	fu_engine_step(&e, FU_CLASS_IO_SEQUENTIAL, 106, &d);
	fu_engine_step(&e, FU_CLASS_IO_SEQUENTIAL, 107, &d);
	fu_engine_step(&e, FU_CLASS_IO_SEQUENTIAL, 108, &d);
	/* streak satisfied but dwell (5 s since 103) is too; exactly 5 s */
	CHECK(d.do_switch && d.to == FU_POLICY_BATCHED);

	fu_engine_init(&e, &c, FU_POLICY_NORMAL, 100.0);
	for (int i = 1; i <= 3; i++)
		fu_engine_step(&e, FU_CLASS_IO_RANDOM, 100 + i, &d);
	fu_engine_commit(&e, d.to, 103);
	for (int i = 1; i <= 3; i++)
		fu_engine_step(&e, FU_CLASS_IDLE, 103 + i, &d);
	CHECK(!d.do_switch && !strcmp(d.why_not, "dwell"));

	/* Static mode switches immediately and ignores the class. */
	c.mode = FU_MODE_STATIC;
	c.static_policy = FU_POLICY_BATCHED;
	fu_engine_step(&e, FU_CLASS_IO_RANDOM, 107, &d);
	CHECK(d.do_switch && d.to == FU_POLICY_BATCHED);

	/* Observe mode always wants NORMAL. */
	c.mode = FU_MODE_OBSERVE;
	fu_engine_step(&e, FU_CLASS_IO_RANDOM, 108, &d);
	CHECK(d.wanted == FU_POLICY_NORMAL);
}

static void test_config(void)
{
	struct fu_config c;
	fu_config_default(&c);
	char text[] = "# comment\ninterval_ms = 250\nhysteresis=5\n"
		      "map.CPU_BOUND = normal\nsmall_io_kb = 8 # trailing\n"
		      "mode = static:specialized\nbogus = 1\nnoequals\n";
	CHECK(fu_config_parse(&c, text) == 2);
	CHECK(c.interval_ms == 250 && c.hysteresis == 5);
	CHECK(c.class_policy[FU_CLASS_CPU_BOUND] == FU_POLICY_NORMAL);
	CHECK(NEAR(c.th.small_io_kb, 8.0));
	CHECK(c.mode == FU_MODE_STATIC && c.static_policy == FU_POLICY_SPECIALIZED);
	CHECK(fu_config_set(&c, "interval_ms", "5") == -1);
	CHECK(fu_config_set_mode(&c, "static:nonsense") == -1);
	CHECK(fu_config_set_mode(&c, "observe") == 0 && c.mode == FU_MODE_OBSERVE);
	CHECK(fu_policy_from_name("fast") == FU_POLICY_SPECIALIZED);
}

static void put(const char *root, const char *rel, const char *val)
{
	char path[512], cmd[600];
	snprintf(path, sizeof(path), "%s%s", root, rel);
	snprintf(cmd, sizeof(cmd), "mkdir -p \"$(dirname '%s')\"", path);
	if (system(cmd) != 0)
		abort();
	FILE *f = fopen(path, "w");
	fputs(val, f);
	fclose(f);
}

static void get(const char *root, const char *rel, char *out, size_t len)
{
	char path[512];
	snprintf(path, sizeof(path), "%s%s", root, rel);
	fu_read_file_abs(path, out, len);
	fu_rtrim(out);
}

static void test_knobs(void)
{
	char root[] = "/tmp/fuhrer-test-XXXXXX";
	CHECK(mkdtemp(root) != NULL);
	put(root, "/sys/block/vda/queue/scheduler", "[mq-deadline] none\n");
	put(root, "/sys/block/vda/queue/read_ahead_kb", "128\n");
	put(root, "/sys/block/vda/queue/nr_requests", "64\n");
	put(root, "/sys/block/vda/queue/nomerges", "0\n");
	put(root, "/sys/block/vda/queue/rq_affinity", "1\n");
	put(root, "/sys/block/loop0/queue/read_ahead_kb", "128\n");
	put(root, "/proc/sys/vm/dirty_background_ratio", "10\n");
	put(root, "/proc/sys/net/core/busy_read", "0\n");
	snprintf(fu_sys_root, sizeof(fu_sys_root), "%s", root);

	char state[600];
	snprintf(state, sizeof(state), "%s/knobs.orig", root);
	struct fu_policy_ctx ctx;
	int n = fu_knobs_init(&ctx, state, 0);
	CHECK(n == 7); /* loop0 excluded, absent knobs skipped */

	char v[64];
	CHECK(fu_policy_specialized.activate(&ctx) == 0);
	get(root, "/sys/block/vda/queue/read_ahead_kb", v, sizeof(v));
	CHECK(!strcmp(v, "16"));
	get(root, "/sys/block/vda/queue/scheduler", v, sizeof(v));
	CHECK(!strcmp(v, "none"));
	get(root, "/proc/sys/net/core/busy_read", v, sizeof(v));
	CHECK(!strcmp(v, "50"));
	get(root, "/sys/block/loop0/queue/read_ahead_kb", v, sizeof(v));
	CHECK(!strcmp(v, "128"));

	CHECK(fu_policy_specialized.deactivate(&ctx) == 0);
	CHECK(fu_policy_batched.activate(&ctx) == 0);
	get(root, "/sys/block/vda/queue/nr_requests", v, sizeof(v));
	CHECK(!strcmp(v, "128")); /* x2 */
	get(root, "/proc/sys/net/core/busy_read", v, sizeof(v));
	CHECK(!strcmp(v, "0"));
	get(root, "/sys/block/vda/queue/read_ahead_kb", v, sizeof(v));
	CHECK(!strcmp(v, "4096"));

	/* Simulate a crash: a new instance must restore the true originals. */
	struct fu_policy_ctx ctx2;
	fu_knobs_init(&ctx2, state, 0);
	CHECK(fu_knobs_restore_all(&ctx2) == 0);
	get(root, "/sys/block/vda/queue/read_ahead_kb", v, sizeof(v));
	CHECK(!strcmp(v, "128"));
	get(root, "/sys/block/vda/queue/scheduler", v, sizeof(v));
	CHECK(!strcmp(v, "mq-deadline"));
	get(root, "/proc/sys/vm/dirty_background_ratio", v, sizeof(v));
	CHECK(!strcmp(v, "10"));

	struct fu_policy_metrics m;
	fu_policy_batched.collect_metrics(&ctx, &m);
	CHECK(m.activations == 1 && m.knobs_failed == 0);

	fu_sys_root[0] = '\0';
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
	if (system(cmd) != 0)
		fprintf(stderr, "warning: cleanup failed\n");
}

int main(void)
{
	test_parsers();
	test_features_and_classifier();
	test_engine();
	test_config();
	test_knobs();
	printf("adaptive unit tests: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
