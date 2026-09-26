// Baseline scheduling policies (NEW_EXPLANATION §16 / §37):
//   round_robin  B0  one FIFO queue, fixed 10 ms quantum
//   priority     B1  32 static priority levels, strict, 10 ms quantum,
//                    a woken higher-priority task preempts
//   low_latency  B2  manually selected "interactive" policy: 2 ms quantum,
//                    woken tasks go to the front and preempt
#include "policy.hpp"

namespace fuhrer {

class RoundRobin final : public Policy {
      public:
	void setup(task *t) override
	{
		t->prio = 0; // single level
		t->quantum_ticks = scaled(10);
		t->wake_preempt = false;
	}
};

class StaticPriority final : public Policy {
      public:
	void setup(task *t) override
	{
		t->prio = t->base_prio;
		t->quantum_ticks = scaled(10);
		t->wake_preempt = true;
	}
	void task_wake(task *t) override
	{
		q_.push_back(t);
		task *cur = sched_current();
		if (cur && !sched_is_idle_task(cur) && t->prio < cur->prio)
			sched_request_resched();
	}
	bool tick(task *cur) override
	{
		// Preempt at quantum end only for an equal-or-better runnable task.
		if (cur->ticks_left)
			cur->ticks_left--;
		if (cur->ticks_left == 0) {
			cur->ticks_left = cur->quantum_ticks;
			return q_.best_prio() <= cur->prio;
		}
		return q_.best_prio() < cur->prio;
	}
};

class LowLatency final : public Policy {
      public:
	void setup(task *t) override
	{
		t->prio = 0;
		t->quantum_ticks = scaled(2);
		t->wake_preempt = true;
	}
	void task_wake(task *t) override
	{
		q_.push_front(t);
		sched_request_resched();
	}
};

} // namespace fuhrer

FUHRER_EXPORT_POLICY(fuhrer::RoundRobin, round_robin,
		     "fixed round-robin, 10 ms quantum (baseline B0)")
FUHRER_EXPORT_POLICY(fuhrer::StaticPriority, priority,
		     "static priorities 0-31, 10 ms quantum, priority preemption (B1)")
FUHRER_EXPORT_POLICY(fuhrer::LowLatency, low_latency,
		     "2 ms quantum, wake-up preemption (manually selected policy B2)")
