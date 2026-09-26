/* Per-task workload profiler (NEW_EXPLANATION §14, M13).
 *
 * Every window (default 100 ms) each task's counters are turned into a class:
 *   share  = CPU time / window           burst = CPU time / times scheduled
 *   blocks = voluntary blocks            io    = I/O syscalls
 * Rules (first match):
 *   ran nothing, no blocks                     -> IDLE
 *   io >= io_min and burst < 5 ms              -> IO_BOUND
 *   blocks >= 2, burst < 2 ms, share < 30 %    -> INTERACTIVE
 *   share >= 70 %, or preempted with no blocks -> CPU_BOUND
 *   otherwise                                  -> MIXED
 * A class change is only applied after `hysteresis` consecutive windows
 * agree. "Confidence" is the share of the last 8 windows that agree with the
 * current class — a stability measure, not a probability. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "sched.h"

static u32 window_ms = 100;
static u32 hysteresis = 2;
static u32 io_min = 4;
static bool enabled = true;
static u64 window_start_ns;
static u64 last_window_ns;
static u64 profiler_cycles;	/* TSC cycles spent classifying (overhead) */
static u64 windows;
static enum task_class sys_cls = CLASS_IDLE, sys_from = CLASS_IDLE;
static enum task_class sys_cand = CLASS_IDLE;
static u32 sys_streak;
static u64 sys_changes, sys_change_ns;

#define ADAPT_LOG 128
static struct adapt_event alog[ADAPT_LOG];
static u32 alog_total;

static const char *names[CLASS_COUNT] = { "UNKNOWN", "IDLE", "INTERACTIVE", "IO_BOUND",
					  "CPU_BOUND", "MIXED" };
const char *task_class_name(enum task_class c) { return c < CLASS_COUNT ? names[c] : "?"; }

void profiler_set_window_ms(u32 ms) { window_ms = ms ? ms : 100; }
u32 profiler_window_ms(void) { return window_ms; }
void profiler_set_hysteresis(u32 h) { hysteresis = h ? h : 1; }
u32 profiler_hysteresis(void) { return hysteresis; }
void profiler_enable(bool on) { enabled = on; }
bool profiler_enabled(void) { return enabled; }
u64 profiler_overhead_cycles(void) { return profiler_cycles; }
u64 profiler_windows(void) { return windows; }

void profiler_init(void) { window_start_ns = time_ns(); }

void adapt_log_add(struct task *t, enum task_class from, enum task_class to)
{
	struct adapt_event *e = &alog[alog_total % ADAPT_LOG];
	e->time_ns = time_ns();
	e->tid = t->tid;
	strlcpy(e->task, t->name, sizeof(e->task));
	e->from = from;
	e->to = to;
	e->reason = t->prof.reason;
	e->confidence = t->prof.confidence;
	alog_total++;
	if (boot_cmdline_has("adapt.log"))
		printk("[ADAPT] t=%lums tid=%d task=%s %s -> %s reason=%s confidence=%u%%\n",
		       e->time_ns / 1000000, t->tid, t->name, task_class_name(from),
		       task_class_name(to), e->reason, e->confidence);
}

u32 adapt_log_read(struct adapt_event *out, u32 max, u32 *total)
{
	u64 f = irq_save();
	u32 n = alog_total < ADAPT_LOG ? alog_total : ADAPT_LOG;
	if (n > max)
		n = max;
	for (u32 i = 0; i < n; i++)
		out[i] = alog[(alog_total - n + i) % ADAPT_LOG];
	if (total)
		*total = alog_total;
	irq_restore(f);
	return n;
}

static enum task_class classify(struct task *t, const struct task_window *w, u64 win_ns,
				const char **why)
{
	u64 share_pm = w->run_ns * 1000 / win_ns;
	u64 burst_ns = w->runs ? w->run_ns / w->runs : 0;
	t->prof.cpu_share_pm = share_pm;
	t->prof.avg_burst_us = burst_ns / 1000;
	if (w->run_ns == 0 && w->blocks == 0 && w->wakeups == 0) {
		*why = "not running";
		return CLASS_IDLE;
	}
	if (w->io_ops >= io_min && burst_ns < 5000000) {
		*why = "frequent I/O, short bursts";
		return CLASS_IO_BOUND;
	}
	if (w->blocks >= 2 && burst_ns < 2000000 && share_pm < 300) {
		*why = "high wake-up frequency, low CPU";
		return CLASS_INTERACTIVE;
	}
	if (share_pm >= 700 || (w->preempts >= 2 && w->blocks == 0)) {
		*why = "uses full quanta, rarely blocks";
		return CLASS_CPU_BOUND;
	}
	*why = "mixed behaviour";
	return CLASS_MIXED;
}

/* Called from the timer interrupt (interrupts disabled). */
void profiler_tick(void)
{
	u64 now = time_ns();
	u64 win_ns = (u64)window_ms * 1000000ULL;
	if (now - window_start_ns < win_ns)
		return;
	u64 c0 = rdtsc();
	u64 real = now - window_start_ns;
	window_start_ns = now;
	last_window_ns = real;
	windows++;
	extern const struct sched_policy *sched_active_policy(void);
	extern void sched_checkpoint_current(u64 now);
	sched_checkpoint_current(now); /* the running slice counts in this window */
	list_for_each(it, &all_tasks) {
		struct task *t = container_of(it, struct task, all_node);
		if (sched_is_idle_task(t) || t->state == TASK_ZOMBIE)
			continue;
		t->prof.last = t->prof.cur;
		memset(&t->prof.cur, 0, sizeof(t->prof.cur));
		if (!enabled)
			continue;
		const char *why;
		enum task_class c = classify(t, &t->prof.last, real, &why);
		t->prof.history[t->prof.hist_pos++ % 8] = c;
		u32 agree = 0, streak = 0;
		for (u32 k = 0; k < 8; k++)
			if (t->prof.history[k] == c)
				agree++;
		for (u32 k = 0; k < hysteresis && k < 8; k++)
			if (t->prof.history[(unsigned)(t->prof.hist_pos + 8 - 1 - k) % 8] == c)
				streak++;
		if (c != t->prof.cls && streak >= hysteresis) {
			enum task_class old = t->prof.cls;
			t->prof.cls = c;
			t->prof.reason = why;
			t->prof.confidence = (u8)(agree * 100 / 8);
			adapt_log_add(t, old, c);
			sched_active_policy()->classified(t);
		} else if (c == t->prof.cls) {
			t->prof.reason = why;
			t->prof.confidence = (u8)(agree * 100 / 8);
		}
	}
	/* system-level class changes (Control Center: "last transition"), with
	 * the same hysteresis as tasks: without it the class flipped between
	 * IDLE and INTERACTIVE on every window while someone typed (173
	 * "transitions" in 84 s of light use). */
	if (enabled) {
		enum task_class sc = profiler_system_class(NULL, NULL);
		if (sc == sys_cls) {
			sys_streak = 0;
		} else if (sc == sys_cand && ++sys_streak >= hysteresis) {
			sys_from = sys_cls;
			sys_cls = sc;
			sys_changes++;
			sys_change_ns = now;
			sys_streak = 0;
		} else if (sc != sys_cand) {
			sys_cand = sc;
			sys_streak = 1;
			if (hysteresis <= 1) {
				sys_from = sys_cls;
				sys_cls = sc;
				sys_changes++;
				sys_change_ns = now;
				sys_streak = 0;
			}
		}
	}
	profiler_cycles += rdtsc() - c0;
}

enum task_class profiler_stable_class(void) { return sys_cls; }

void profiler_system_transitions(u64 *count, u64 *last_ns, enum task_class *from, enum task_class *to)
{
	*count = sys_changes;
	*last_ns = sys_change_ns;
	*from = sys_from;
	*to = sys_cls;
}

/* System-level view: the class holding most CPU time in the last window,
 * with "interactive" winning ties when interactive tasks are active. */
enum task_class profiler_system_class(u8 *confidence, const char **reason)
{
	/* Weight = activity in the last window: CPU milliseconds + wake-ups +
	 * I/O operations. CPU time alone would make tasks that mostly block
	 * (interactive, network, disk) invisible next to any computation. */
	u64 by_class[CLASS_COUNT] = { 0 };
	u32 interactive = 0;
	u64 total = 0;
	u64 f = irq_save();
	list_for_each(it, &all_tasks) {
		struct task *t = container_of(it, struct task, all_node);
		if (sched_is_idle_task(t) || t->state == TASK_ZOMBIE)
			continue;
		const struct task_window *w = &t->prof.last;
		u64 score = w->run_ns / 1000000 + w->wakeups + w->io_ops;
		by_class[t->prof.cls] += score;
		total += score;
		if (t->prof.cls == CLASS_INTERACTIVE)
			interactive++;
	}
	irq_restore(f);
	by_class[CLASS_UNKNOWN] = 0;
	enum task_class best = CLASS_IDLE;
	for (int c = CLASS_INTERACTIVE; c < CLASS_COUNT; c++)
		if (by_class[c] > by_class[best])
			best = (enum task_class)c;
	/* Housekeeping threads (network daemon, flusher, compositor) wake a few
	 * times per window; below this much activity the system is idle. */
	u64 floor = profiler_window_ms() / 10 + 2;
	if (by_class[best] < floor)
		best = CLASS_IDLE;
	if (confidence)
		*confidence = total ? (u8)(by_class[best] * 100 / total) : 100;
	if (reason)
		*reason = best == CLASS_IDLE ? "little CPU activity"
			  : interactive && best != CLASS_INTERACTIVE ? "dominant class (interactive tasks present)"
								      : "dominant class by CPU time";
	return best;
}
