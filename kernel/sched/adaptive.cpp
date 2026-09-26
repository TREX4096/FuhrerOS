// FuhrerOS adaptive scheduler (research component, NEW_EXPLANATION §14/§16).
//
// The profiler (profiler.c) classifies every task each window from what it
// actually did: CPU share, burst length, voluntary blocks, I/O operations.
// This policy maps the class to scheduling parameters:
//
//   class        prio  quantum  wake-preempt   intent
//   INTERACTIVE     4     3 ms  yes            low response latency
//   IO_BOUND        8     5 ms  yes            fast wake-up, keeps devices busy
//   MIXED/UNKNOWN  12     8 ms  no             neutral
//   CPU_BOUND      20    30 ms  no             fewer context switches
//
// Anti-starvation: a task waiting more than aging_ms in the run queue is
// temporarily lifted to priority 12 so CPU-bound work always progresses.
// Base priority (user "nice") shifts the band by (base-16)/4.
#include "policy.hpp"

namespace fuhrer {

struct ClassParams {
	int prio;
	u32 quantum;
	bool wake_preempt;
};

static const ClassParams params[CLASS_COUNT] = {
	/* UNKNOWN     */ {12, 8, false},
	/* IDLE        */ {12, 8, false},
	/* INTERACTIVE */ {4, 3, true},
	/* IO_BOUND    */ {8, 5, true},
	/* CPU_BOUND   */ {20, 30, false},
	/* MIXED       */ {12, 8, false},
};

class Adaptive final : public Policy {
      public:
	void init() override
	{
		Policy::init();
		aging_ticks_ = 0;
	}
	void setup(task *t) override
	{
		const ClassParams &p = params[t->prof.cls];
		int prio = p.prio + (t->base_prio - 16) / 4;
		t->prio = prio < 0 ? 0 : (prio > 31 ? 31 : prio);
		t->quantum_ticks = scaled(p.quantum);
		t->wake_preempt = p.wake_preempt;
	}
	void task_wake(task *t) override
	{
		setup(t); // aging boost ends when the task runs/blocks
		q_.push_back(t);
		task *cur = sched_current();
		if (t->wake_preempt && cur && !sched_is_idle_task(cur) && t->prio < cur->prio)
			sched_request_resched();
	}
	void requeue(task *t) override
	{
		if (!t->ticks_left) {
			setup(t);
			t->ticks_left = t->quantum_ticks;
		}
		q_.push_back(t);
	}
	bool tick(task *cur) override
	{
		if (++aging_ticks_ >= 10) {
			aging_ticks_ = 0;
			age();
		}
		if (cur->ticks_left)
			cur->ticks_left--;
		if (cur->ticks_left == 0) {
			cur->ticks_left = cur->quantum_ticks;
			return q_.best_prio() <= cur->prio;
		}
		return q_.best_prio() < cur->prio;
	}
	void classified(task *t) override
	{
		// New class: move a queued task to its new level immediately.
		bool queued = t->state == TASK_RUNNABLE && list_linked(&t->run_node);
		if (queued)
			q_.remove(t);
		setup(t);
		if (queued)
			q_.push_back(t);
	}

      private:
	void age()
	{
		const u64 now = time_ns(), limit = 100ULL * 1000000ULL;
		task *boost[16];
		int n = 0;
		q_.for_each([&](task *t) {
			if (n < 16 && t->prio > 12 && now - t->enqueue_ns > limit)
				boost[n++] = t;
		});
		for (int i = 0; i < n; i++) {
			q_.remove(boost[i]);
			boost[i]->prio = 12;
			q_.push_back(boost[i]);
		}
	}
	u32 aging_ticks_ = 0;
};

} // namespace fuhrer

FUHRER_EXPORT_POLICY(fuhrer::Adaptive, adaptive,
		     "workload-aware: per-task class -> priority/quantum/wake-preemption (B3)")

extern "C" {
extern const struct sched_policy round_robin_policy, priority_policy, low_latency_policy,
	adaptive_policy;
const struct sched_policy *sched_policies[] = {&round_robin_policy, &priority_policy,
					       &low_latency_policy, &adaptive_policy};
const u32 sched_policy_count = 4;
int sched_quantum_scale(void);
}

int fuhrer::quantum_scale() { return sched_quantum_scale(); }
