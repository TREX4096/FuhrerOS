/* M4 self tests: kernel threads, context switching, sleeping, mutexes, wait
 * queues, run-time policy switching and profiler classification. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "sched.h"

static volatile u64 spin_count[4];
static volatile bool stop_spin;
static volatile int done_count;

static void spinner(void *arg)
{
	int id = (int)(u64)arg;
	while (!stop_spin)
		spin_count[id]++;
	done_count++;
}

static bool run_concurrency(const char *policy, u64 *switches_out, u64 *min_share_pm)
{
	sched_set_policy(policy);
	struct sched_stats s0, s1;
	sched_get_stats(&s0);
	stop_spin = false;
	done_count = 0;
	for (int i = 0; i < 3; i++) {
		spin_count[i] = 0;
		task_create_kernel("spin", spinner, (void *)(u64)i);
	}
	sleep_ms(300);
	stop_spin = true;
	while (done_count < 3)
		sleep_ms(1);
	sched_get_stats(&s1);
	u64 total = spin_count[0] + spin_count[1] + spin_count[2];
	u64 mn = spin_count[0];
	for (int i = 1; i < 3; i++)
		if (spin_count[i] < mn)
			mn = spin_count[i];
	*switches_out = s1.context_switches - s0.context_switches;
	*min_share_pm = total ? mn * 1000 / total : 0;
	return spin_count[0] && spin_count[1] && spin_count[2];
}

static struct mutex mtx;
static volatile u64 shared_counter;

static void mutex_worker(void *arg)
{
	for (int i = 0; i < 2000; i++) {
		mutex_lock(&mtx);
		u64 v = shared_counter;
		if ((i & 63) == 0)
			yield(); /* try hard to interleave inside the critical section */
		shared_counter = v + 1;
		mutex_unlock(&mtx);
	}
	done_count++;
}

static struct waitqueue wq;
static volatile int produced, consumed;

static void consumer(void *arg)
{
	while (consumed < 50) {
		u64 f = irq_save();
		while (produced == consumed)
			wq_wait(&wq, 0);
		consumed++;
		irq_restore(f);
	}
	done_count++;
}

static volatile u64 inter_iters;
static void interactive_like(void *arg)
{
	while (!stop_spin) {
		sleep_ms(3);
		for (volatile int i = 0; i < 2000; i++)
			;
		inter_iters++;
	}
	done_count++;
}

void test_scheduler(void)
{
	u64 sw, share;
	const char *pols[] = { "round_robin", "priority", "low_latency", "adaptive" };
	for (int i = 0; i < 4; i++) {
		bool ok = run_concurrency(pols[i], &sw, &share);
		char name[48];
		snprintf(name, sizeof(name), "sched.concurrent.%s", pols[i]);
		/* fair share of 3 equal spinners is 333 per mille */
		selftest_report(name, ok && share > 200, "%lu switches in 300 ms, min share %lu/1000",
				sw, share);
	}

	u64 t0 = time_ns();
	sleep_ms(20);
	u64 el = time_ns() - t0;
	selftest_report("sched.sleep_accuracy", el >= 20000000 && el < 26000000,
			"sleep_ms(20) took %lu us", el / 1000);

	mutex_init(&mtx);
	shared_counter = 0;
	done_count = 0;
	for (int i = 0; i < 3; i++)
		task_create_kernel("mutex", mutex_worker, NULL);
	while (done_count < 3)
		sleep_ms(2);
	selftest_report("sync.mutex", shared_counter == 6000, "counter=%lu (expected 6000)",
			shared_counter);

	wq_init(&wq);
	produced = consumed = 0;
	done_count = 0;
	task_create_kernel("consumer", consumer, NULL);
	for (int i = 0; i < 50; i++) {
		produced++;
		wq_wake_one(&wq);
		if (i % 7 == 0)
			sleep_ms(1);
	}
	u64 deadline = time_ns() + 500000000ULL;
	while (done_count < 1 && time_ns() < deadline)
		sleep_ms(1);
	selftest_report("sync.waitqueue", consumed == 50, "consumed %d/50", consumed);

	/* Profiler: a spinner should become CPU_BOUND, a 3 ms sleeper INTERACTIVE. */
	sched_set_policy("adaptive");
	stop_spin = false;
	done_count = 0;
	spin_count[0] = 0;
	struct task *cpu = task_create_kernel("cpu-hog", spinner, (void *)0);
	struct task *ui = task_create_kernel("ui-like", interactive_like, NULL);
	sleep_ms(700);
	enum task_class cc = cpu->prof.cls, uc = ui->prof.cls;
	u8 conf_c = cpu->prof.confidence, conf_u = ui->prof.confidence;
	stop_spin = true;
	while (done_count < 2)
		sleep_ms(2);
	selftest_report("profiler.classify.cpu", cc == CLASS_CPU_BOUND, "cpu-hog -> %s (%u%%)",
			task_class_name(cc), conf_c);
	selftest_report("profiler.classify.interactive", uc == CLASS_INTERACTIVE,
			"ui-like -> %s (%u%%)", task_class_name(uc), conf_u);
	u32 total = 0;
	struct adapt_event ev[4];
	adapt_log_read(ev, 4, &total);
	selftest_report("profiler.adapt_log", total >= 2, "%u class transitions logged", total);
}
