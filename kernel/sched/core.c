/* Scheduler core: task lifecycle, context switching, sleeping, blocking,
 * preemption and accounting. Policy decisions are delegated to the active
 * `struct sched_policy`. Uniprocessor: disabling interrupts is the lock. */
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/percpu.h"
#include "kernel.h"
#include "mm.h"
#include "proc.h"
#include "sched.h"

#define KSTACK_SIZE (32 * 1024)

void context_switch(u64 *save_rsp, u64 new_rsp);
extern char task_trampoline[];

struct percpu bsp_percpu;
struct list_node all_tasks = LIST_INIT(all_tasks);
static struct list_node sleepers = LIST_INIT(sleepers);
static const struct sched_policy *policy;
static struct task *idle_task;
static struct task *dead_task;
static volatile bool need_resched;
static int next_tid = 1;
static struct sched_stats st;
static int quantum_scale_pct = 100;
static bool started;

/* Until the process layer exists (M5) nothing owns user threads. */
__attribute__((weak)) void proc_thread_exited(struct task *t) {}

void percpu_init(void)
{
	bsp_percpu.self = (u64)&bsp_percpu;
	wrmsr(MSR_GS_BASE, (u64)&bsp_percpu);
	wrmsr(MSR_KERNEL_GS_BASE, 0);
}

static inline struct task *current_task(void) { return bsp_percpu.current; }
struct task *sched_current(void) { return bsp_percpu.current; }
bool sched_is_idle_task(struct task *t) { return t == idle_task; }
void sched_request_resched(void) { need_resched = true; }
void sched_set_quantum_scale(int pct) { quantum_scale_pct = pct > 0 ? pct : 100; }
int sched_quantum_scale(void) { return quantum_scale_pct; }

const char *sched_current_task_name(void)
{
	return current_task() ? current_task()->name : "(boot)";
}
const char *sched_current_process_name(void)
{
	struct task *t = current_task();
	return t && t->proc ? t->proc->name : "(kernel)";
}
int sched_current_tid(void) { return current_task() ? current_task()->tid : 0; }

struct task *task_by_tid(int tid)
{
	list_for_each(it, &all_tasks) {
		struct task *t = container_of(it, struct task, all_node);
		if (t->tid == tid)
			return t;
	}
	return NULL;
}

/* Prepare a fresh kernel stack so the first context_switch "returns" into
 * task_trampoline(fn, arg). */
void sched_prepare_new_task(struct task *t, void (*fn)(void *), void *arg)
{
	u64 *sp = (u64 *)t->kstack_top;
	*--sp = 0;			/* alignment: trampoline entered like a call */
	*--sp = (u64)task_trampoline;	/* ret target */
	*--sp = 0;			/* rbp */
	*--sp = 0;			/* rbx */
	*--sp = (u64)fn;		/* r12 */
	*--sp = (u64)arg;		/* r13 */
	*--sp = 0;			/* r14 */
	*--sp = 0;			/* r15 */
	t->saved_rsp = (u64)sp;
}

static struct task *task_alloc(const char *name)
{
	struct task *t = kzalloc(sizeof(*t));
	if (!t)
		return NULL;
	t->kstack_size = KSTACK_SIZE;
	t->kstack_base = kstack_alloc(KSTACK_SIZE, &t->kstack_top);
	if (!t->kstack_base) {
		kfree(t);
		return NULL;
	}
	strlcpy(t->name, name, sizeof(t->name));
	t->base_prio = 16;
	t->prio = 16;
	t->created_ns = time_ns();
	list_init(&t->run_node);
	list_init(&t->wait_node);
	list_init(&t->sleep_node);
	list_init(&t->proc_node);
	u64 f = irq_save();
	t->tid = next_tid++;
	list_push_back(&all_tasks, &t->all_node);
	st.tasks++;
	irq_restore(f);
	return t;
}

/* Create a task (kernel thread, or the first thread of a process when the
 * caller fills in t->proc before it runs). It is runnable immediately. */
struct task *task_create_kernel(const char *name, void (*fn)(void *), void *arg)
{
	struct task *t = task_alloc(name);
	if (!t)
		return NULL;
	sched_prepare_new_task(t, fn, arg);
	u64 f = irq_save();
	t->state = TASK_RUNNABLE;
	t->enqueue_ns = time_ns();
	if (policy)
		policy->task_create(t);
	irq_restore(f);
	return t;
}

/* Like task_create_kernel but not yet runnable (process setup finishes first). */
struct task *task_create_suspended(const char *name, void (*fn)(void *), void *arg)
{
	struct task *t = task_alloc(name);
	if (!t)
		return NULL;
	sched_prepare_new_task(t, fn, arg);
	t->state = TASK_BLOCKED;
	if (policy)
		policy->setup(t);
	return t;
}

void task_start(struct task *t)
{
	u64 f = irq_save();
	t->state = TASK_RUNNABLE;
	t->enqueue_ns = time_ns();
	policy->task_create(t);
	irq_restore(f);
}

/* Runs in the context of the task that was just switched to. */
void sched_finish_switch(void)
{
	if (dead_task) {
		struct task *d = dead_task;
		dead_task = NULL;
		kstack_free(d->kstack_base, d->kstack_size);
		d->kstack_base = NULL;
		if (!d->proc) { /* kernel threads have nobody to reap them */
			list_remove(&d->all_node);
			st.tasks--;
			kfree(d);
		}
	}
}

static void account_out(struct task *prev, u64 now)
{
	u64 ran = now - prev->run_start_ns;
	prev->cpu_ns += ran;
	if (prev == idle_task) {
		st.idle_ns += ran;
	} else {
		st.busy_ns += ran;
		prev->prof.cur.run_ns += ran;
	}
}

/* Charge the running task's slice so far (profiling window boundaries). */
void sched_checkpoint_current(u64 now)
{
	struct task *cur = current_task();
	if (!cur)
		return;
	account_out(cur, now);
	cur->run_start_ns = now;
}

const struct sched_policy *sched_active_policy(void) { return policy; }

static void switch_to(struct task *prev, struct task *next)
{
	u64 now = time_ns();
	account_out(prev, now);
	next->state = TASK_RUNNING;
	next->run_start_ns = now;
	next->switches++;
	next->prof.cur.runs++;
	if (next->woken_ns) {
		next->prof.cur.wake_lat_ns += now - next->woken_ns;
		next->last_dispatch_ns = now - next->woken_ns;
		next->woken_ns = 0;
	}
	bsp_percpu.current = next;
	bsp_percpu.kernel_rsp = next->kstack_top;
	tss_set_kernel_stack(next->kstack_top);
	if (next->proc)
		vmm_switch(next->proc->as);
	else if (prev->proc)
		vmm_switch(vmm_kernel_space());
	st.context_switches++;
	context_switch(&prev->saved_rsp, next->saved_rsp);
	sched_finish_switch();
}

void schedule(void)
{
	u64 f = irq_save();
	u64 c0 = rdtsc();
	struct task *prev = current_task();
	need_resched = false;
	st.sched_calls++;
	if (prev->state == TASK_RUNNING) {
		prev->state = TASK_RUNNABLE;
		if (prev != idle_task) {
			prev->enqueue_ns = time_ns();
			policy->requeue(prev);
		}
	}
	struct task *next = policy->select_next();
	if (!next)
		next = idle_task;
	st.sched_cycles += rdtsc() - c0;
	if (next != prev)
		switch_to(prev, next);
	else
		prev->state = TASK_RUNNING;
	irq_restore(f);
}

void yield(void) { schedule(); }

/* Timer interrupt: wake sleepers, profile, and let the policy decide. */
void sched_timer_tick(void)
{
	if (!started)
		return;
	u64 now = time_ns();
	while (!list_empty(&sleepers)) {
		struct task *t = container_of(sleepers.next, struct task, sleep_node);
		if (t->wake_at_ns > now)
			break;
		t->wake_reason = -E_TIMEDOUT;
		task_wake(t);
	}
	profiler_tick();
	struct task *cur = current_task();
	if (cur == idle_task) {
		if (policy->runnable())
			need_resched = true;
	} else if (policy->tick(cur)) {
		need_resched = true;
	}
}

/* Called at the end of every interrupt: the preemption point. */
void sched_irq_exit(struct trap_frame *tf)
{
	if (!started || !need_resched)
		return;
	struct task *cur = current_task();
	if (cur != idle_task && cur->state == TASK_RUNNING) {
		cur->prof.cur.preempts++;
		st.preemptions++;
	}
	schedule();
}

static void sleep_insert(struct task *t)
{
	list_for_each(it, &sleepers) {
		struct task *o = container_of(it, struct task, sleep_node);
		if (o->wake_at_ns > t->wake_at_ns) {
			list_insert_between(&t->sleep_node, it->prev, it);
			return;
		}
	}
	list_push_back(&sleepers, &t->sleep_node);
}

void task_block_current(void)
{
	struct task *t = current_task();
	t->prof.cur.blocks++;
	policy->task_block(t);
	schedule();
}

void sleep_ns(u64 ns)
{
	u64 f = irq_save();
	struct task *t = current_task();
	t->wake_at_ns = time_ns() + ns;
	t->state = TASK_SLEEPING;
	sleep_insert(t);
	task_block_current();
	irq_restore(f);
}

void sleep_ms(u64 ms) { sleep_ns(ms * 1000000ULL); }

void task_wake(struct task *t)
{
	u64 f = irq_save();
	if (t->state == TASK_SLEEPING || t->state == TASK_BLOCKED) {
		if (list_linked(&t->sleep_node))
			list_remove(&t->sleep_node);
		if (list_linked(&t->wait_node))
			list_remove(&t->wait_node);
		t->state = TASK_RUNNABLE;
		t->woken_ns = time_ns();
		t->enqueue_ns = t->woken_ns;
		t->prof.cur.wakeups++;
		policy->task_wake(t);
		/* A wake-up from a device IRQ while idle must switch at this
		 * interrupt's exit, not at the next tick (F-118: every blocking
		 * disk read cost ~1 ms). */
		if (current_task() == idle_task)
			need_resched = true;
	}
	irq_restore(f);
}

NORETURN void task_exit(int code)
{
	struct task *t = current_task();
	t->exit_code = code;
	/* Process teardown may sleep (locks, I/O), so it runs while the task is
	 * still a normal running task; only then does it become a zombie. */
	if (t->proc)
		proc_thread_exited(t);
	cli();
	t->state = TASK_ZOMBIE;
	policy->task_exit(t);
	dead_task = t;
	schedule();
	panic("zombie task %d rescheduled", t->tid);
}

NORETURN void task_exit_from_trampoline(int code) { task_exit(code); }

/* Free a zombie task that belonged to a process (called by proc reaping). */
void task_reap(struct task *t)
{
	u64 f = irq_save();
	list_remove(&t->all_node);
	if (list_linked(&t->proc_node))
		list_remove(&t->proc_node);
	st.tasks--;
	irq_restore(f);
	kfree(t);
}

void task_set_priority(struct task *t, int prio)
{
	if (prio < 0)
		prio = 0;
	if (prio > 31)
		prio = 31;
	u64 f = irq_save();
	t->base_prio = prio;
	bool queued = t->state == TASK_RUNNABLE && list_linked(&t->run_node);
	if (queued)
		policy->remove(t);
	policy->setup(t);
	if (queued)
		policy->requeue(t);
	irq_restore(f);
}

void sched_account_io(void)
{
	struct task *t = current_task();
	if (t)
		t->prof.cur.io_ops++;
}

/* Switch policy at run time: every task gets the new policy's parameters,
 * and runnable ones move to its run queue. */
int sched_set_policy(const char *name)
{
	const struct sched_policy *np = NULL;
	for (u32 i = 0; i < sched_policy_count; i++)
		if (!strcmp(sched_policies[i]->name, name))
			np = sched_policies[i];
	if (!np)
		return -E_INVAL;
	u64 f = irq_save();
	const struct sched_policy *old = policy;
	if (old == np) {
		irq_restore(f);
		return 0;
	}
	struct task *queued[256];
	u32 nq = 0;
	if (old) {
		struct task *t;
		while ((t = old->select_next()) && nq < 256)
			queued[nq++] = t;
	}
	policy = np;
	policy->init();
	list_for_each(it, &all_tasks) {
		struct task *t = container_of(it, struct task, all_node);
		if (t != idle_task)
			policy->setup(t);
	}
	for (u32 i = 0; i < nq; i++)
		policy->requeue(queued[i]);
	st.policy_switches++;
	need_resched = true;
	irq_restore(f);
	KLOG("sched", "policy -> %s", np->name);
	return 0;
}

const char *sched_policy_name(void) { return policy ? policy->name : "none"; }

void sched_get_stats(struct sched_stats *s)
{
	u64 f = irq_save();
	*s = st;
	s->runnable = policy ? policy->runnable() : 0;
	/* include the running slice so utilisation is current */
	struct task *cur = current_task();
	if (cur) {
		u64 ran = time_ns() - cur->run_start_ns;
		if (cur == idle_task)
			s->idle_ns += ran;
		else
			s->busy_ns += ran;
	}
	irq_restore(f);
}

/* ---- wait queues ---- */
void wq_init(struct waitqueue *wq) { list_init(&wq->waiters); }

int wq_wait(struct waitqueue *wq, u64 timeout_ns)
{
	u64 f = irq_save();
	struct task *t = current_task();
	t->state = TASK_BLOCKED;
	t->wake_reason = 0;
	list_push_back(&wq->waiters, &t->wait_node);
	if (timeout_ns) {
		t->wake_at_ns = time_ns() + timeout_ns;
		sleep_insert(t);
	}
	task_block_current();
	int r = t->wake_reason;
	irq_restore(f);
	return r;
}

void wq_wake_one(struct waitqueue *wq)
{
	u64 f = irq_save();
	if (!list_empty(&wq->waiters))
		task_wake(container_of(wq->waiters.next, struct task, wait_node));
	irq_restore(f);
}

void wq_wake_all(struct waitqueue *wq)
{
	u64 f = irq_save();
	while (!list_empty(&wq->waiters))
		task_wake(container_of(wq->waiters.next, struct task, wait_node));
	irq_restore(f);
}

void mutex_init(struct mutex *m)
{
	m->owner = NULL;
	wq_init(&m->wq);
}

void mutex_lock(struct mutex *m)
{
	u64 f = irq_save();
	while (m->owner)
		wq_wait(&m->wq, 0);
	m->owner = current_task();
	irq_restore(f);
}

void mutex_unlock(struct mutex *m)
{
	u64 f = irq_save();
	m->owner = NULL;
	wq_wake_one(&m->wq);
	irq_restore(f);
}

void sem_init(struct semaphore *s, int count)
{
	s->count = count;
	wq_init(&s->wq);
}

void sem_down(struct semaphore *s)
{
	u64 f = irq_save();
	while (s->count <= 0)
		wq_wait(&s->wq, 0);
	s->count--;
	irq_restore(f);
}

void sem_up(struct semaphore *s)
{
	u64 f = irq_save();
	s->count++;
	wq_wake_one(&s->wq);
	irq_restore(f);
}

/* ---- bring-up ---- */
static void idle_loop(void *arg)
{
	for (;;) {
		sti();
		hlt();
	}
}

void sched_init(void)
{
	percpu_init();
	sched_set_policy(boot_cmdline_has("sched=rr")	     ? "round_robin"
			 : boot_cmdline_has("sched=priority")    ? "priority"
			 : boot_cmdline_has("sched=lowlatency")  ? "low_latency"
								 : "adaptive");
	idle_task = task_alloc("idle");
	sched_prepare_new_task(idle_task, idle_loop, NULL);
	idle_task->state = TASK_RUNNABLE;
	idle_task->prio = 99;
	profiler_init();
	KLOG("sched", "scheduler ready, policy %s, %u policies available", policy->name,
	     sched_policy_count);
}

/* Leave the boot stack for good and run the first task. */
NORETURN void sched_start(void)
{
	cli();
	struct task *first = policy->select_next();
	if (!first)
		first = idle_task;
	started = true;
	first->state = TASK_RUNNING;
	first->run_start_ns = time_ns();
	bsp_percpu.current = first;
	bsp_percpu.kernel_rsp = first->kstack_top;
	tss_set_kernel_stack(first->kstack_top);
	u64 dummy;
	context_switch(&dummy, first->saved_rsp);
	panic("sched_start returned");
}
