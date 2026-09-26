/* FuhrerOS tasks and the modular scheduler (NEW_EXPLANATION §13, §16, §17).
 *
 * A task is a kernel-schedulable thread; a process (proc.h) owns an address
 * space, file descriptors and one or more tasks. Scheduling *policy* is
 * pluggable: the core (sched/core.c) does context switches, sleeping,
 * blocking and accounting, and calls the active policy through
 * `struct sched_policy`. Policies: round_robin, priority, low_latency,
 * adaptive (C++ classes in the sched directory). */
#ifndef FUHRER_SCHED_H
#define FUHRER_SCHED_H

#include "list.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum task_state { TASK_RUNNABLE, TASK_RUNNING, TASK_BLOCKED, TASK_SLEEPING, TASK_ZOMBIE };

/* Workload classes assigned by the profiler (sched/profiler.c). */
enum task_class {
	CLASS_UNKNOWN = 0,
	CLASS_IDLE,
	CLASS_INTERACTIVE,
	CLASS_IO_BOUND,
	CLASS_CPU_BOUND,
	CLASS_MIXED,
	CLASS_COUNT
};

/* Per-task counters over the current profiling window. */
struct task_window {
	u64 run_ns;
	u32 runs;		/* times scheduled in */
	u32 blocks;		/* voluntary sleeps/blocks */
	u32 preempts;		/* involuntary (quantum expiry) */
	u32 wakeups;
	u32 io_ops;		/* block/net/tty I/O syscalls */
	u64 wake_lat_ns;	/* sum of wake -> run latency */
};

struct task_profile {
	struct task_window cur, last;
	enum task_class cls;
	enum task_class history[8];	/* last classifications (confidence) */
	u8 hist_pos;
	u8 confidence;			/* % of recent windows agreeing */
	const char *reason;
	u64 cpu_share_pm;		/* last window, per mille of one CPU */
	u64 avg_burst_us;
};

struct process;

struct task {
	int tid;
	char name[32];
	enum task_state state;
	u64 saved_rsp;			/* kernel stack pointer while switched out */
	void *kstack_base;
	u64 kstack_top;
	u64 kstack_size;
	struct process *proc;		/* NULL for pure kernel threads */
	bool user;			/* has a ring-3 context */

	/* scheduling parameters (owned by the active policy) */
	int base_prio;			/* 0 = highest, 31 = lowest; default 16 */
	int prio;			/* effective priority */
	u32 quantum_ticks;
	u32 ticks_left;
	bool wake_preempt;		/* may preempt the running task on wake-up */
	u64 enqueue_ns;			/* when it became runnable (aging) */
	struct list_node run_node;	/* policy run queue */
	struct list_node all_node;	/* global task list */
	struct list_node wait_node;	/* wait queue */
	struct list_node sleep_node;	/* timed sleep list */
	u64 wake_at_ns;			/* sleeping: absolute wake time */
	void *wait_channel;
	int wake_reason;

	/* accounting */
	u64 cpu_ns;
	u64 run_start_ns;
	u64 woken_ns;			/* last wake-up, for wake latency */
	u64 last_dispatch_ns;		/* latest wake -> running latency */
	u64 switches;
	u64 created_ns;
	struct task_profile prof;
	int exit_code;
	struct list_node proc_node;	/* thread list of the owning process */
};

/* Scheduler policy interface. The spec's scheduler_init / task_create /
 * task_block / task_wake / task_exit / scheduler_tick / scheduler_select_next
 * map onto init / task_create / task_block / task_wake / task_exit / tick /
 * select_next; setup / requeue / remove support preemption and switching
 * policies at run time. All hooks run with interrupts disabled. */
struct sched_policy {
	const char *name;
	const char *description;
	void (*init)(void);
	void (*setup)(struct task *t);		/* assign quantum/priority, no enqueue */
	void (*task_create)(struct task *t);	/* new runnable task: setup + enqueue */
	void (*task_wake)(struct task *t);	/* woken: enqueue (may request preemption) */
	void (*requeue)(struct task *t);	/* preempted/yielded: enqueue */
	void (*task_block)(struct task *t);	/* notification: t blocked voluntarily */
	void (*task_exit)(struct task *t);	/* notification: t exited */
	void (*remove)(struct task *t);		/* take a queued task off the run queue */
	bool (*tick)(struct task *cur);		/* timer tick; true = preempt cur */
	struct task *(*select_next)(void);	/* dequeue the next task, NULL if none */
	void (*classified)(struct task *t);	/* profiler assigned a new class */
	u32 (*runnable)(void);
};

void sched_request_resched(void);	/* policies: preempt at the next safe point */
bool sched_is_idle_task(struct task *t);

extern const struct sched_policy *sched_policies[];
extern const u32 sched_policy_count;

/* ---- core API ---- */
void sched_init(void);
struct task *task_create_kernel(const char *name, void (*fn)(void *), void *arg);
struct task *sched_current(void);
void sched_start(void) __attribute__((noreturn));
void schedule(void);
void yield(void);
void sleep_ns(u64 ns);
void sleep_ms(u64 ms);
void task_exit(int code) __attribute__((noreturn));
void task_block_current(void);		/* caller set state + queued itself */
void task_wake(struct task *t);
void task_set_priority(struct task *t, int prio);
int sched_set_policy(const char *name);
const char *sched_policy_name(void);
struct task *task_by_tid(int tid);
void task_reap(struct task *t);
void sched_account_io(void);		/* current task did an I/O operation */
void sched_prepare_new_task(struct task *t, void (*fn)(void *), void *arg);
void sched_set_quantum_scale(int pct);	/* ablations */

/* statistics */
struct sched_stats {
	u64 context_switches;
	u64 preemptions;
	u64 sched_calls;
	u64 sched_cycles;		/* TSC cycles spent in schedule() */
	u64 idle_ns;
	u64 busy_ns;
	u64 policy_switches;
	u32 tasks;
	u32 runnable;
};
void sched_get_stats(struct sched_stats *s);
extern struct list_node all_tasks;

/* ---- wait queues and synchronisation ---- */
struct waitqueue {
	struct list_node waiters;
};
void wq_init(struct waitqueue *wq);
/* Sleep on wq until woken (returns 0) or until timeout_ns elapses (returns
 * -E_TIMEDOUT); timeout_ns = 0 waits forever. Caller must re-check its
 * condition. Interrupts must be disabled by the caller around the check. */
int wq_wait(struct waitqueue *wq, u64 timeout_ns);
void wq_wake_one(struct waitqueue *wq);
void wq_wake_all(struct waitqueue *wq);

#define WAIT_EVENT(wq, cond)                          \
	do {                                          \
		u64 __f = irq_save();                 \
		while (!(cond))                       \
			wq_wait((wq), 0);             \
		irq_restore(__f);                     \
	} while (0)

struct mutex {
	struct task *owner;
	struct waitqueue wq;
};
void mutex_init(struct mutex *m);
void mutex_lock(struct mutex *m);
void mutex_unlock(struct mutex *m);

struct semaphore {
	int count;
	struct waitqueue wq;
};
void sem_init(struct semaphore *s, int count);
void sem_down(struct semaphore *s);
void sem_up(struct semaphore *s);

/* ---- profiler / adaptive (sched/profiler.c) ---- */
void profiler_init(void);
void profiler_tick(void);			/* called from the timer */
void profiler_set_window_ms(u32 ms);
u32 profiler_window_ms(void);
void profiler_enable(bool on);
bool profiler_enabled(void);
const char *task_class_name(enum task_class c);
enum task_class profiler_system_class(u8 *confidence, const char **reason);
/* The system class after hysteresis (what the desktop shows). */
enum task_class profiler_stable_class(void);
/* System-level class changes since boot and the time of the last one. */
void profiler_system_transitions(u64 *count, u64 *last_ns, enum task_class *from, enum task_class *to);
struct adapt_event {
	u64 time_ns;
	int tid;
	char task[16];
	enum task_class from, to;
	const char *reason;
	u8 confidence;
};
u32 adapt_log_read(struct adapt_event *out, u32 max, u32 *total);
void adapt_log_add(struct task *t, enum task_class from, enum task_class to);

#ifdef __cplusplus
}
#endif
#endif
