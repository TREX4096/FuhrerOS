// Common machinery for C++ scheduling policies.
//
// A policy is a class deriving from Policy; FUHRER_EXPORT_POLICY builds the
// C `struct sched_policy` table whose function pointers forward to a single
// static instance, so the C scheduler core never sees C++.
#pragma once

extern "C" {
#include "kernel.h"
#include "sched.h"
}

namespace fuhrer {

int quantum_scale();

// 32-level priority run queue with a bitmap for O(1) highest-priority lookup.
class PrioQueue {
      public:
	void reset()
	{
		for (auto &l : levels_)
			list_init(&l);
		bitmap_ = 0;
		count_ = 0;
	}
	void push_back(task *t) { push(t, false); }
	void push_front(task *t) { push(t, true); }
	task *pop()
	{
		if (!bitmap_)
			return nullptr;
		int p = __builtin_ctz(bitmap_);
		list_node *n = list_pop_front(&levels_[p]);
		if (list_empty(&levels_[p]))
			bitmap_ &= ~(1u << p);
		count_--;
		return container_of(n, task, run_node);
	}
	void remove(task *t)
	{
		if (!list_linked(&t->run_node))
			return;
		int p = level(t);
		list_remove(&t->run_node);
		if (list_empty(&levels_[p]))
			bitmap_ &= ~(1u << p);
		count_--;
	}
	int best_prio() const { return bitmap_ ? __builtin_ctz(bitmap_) : 99; }
	u32 count() const { return count_; }
	// Visit queued tasks (for aging); callback may not modify the queue.
	template <typename F> void for_each(F f)
	{
		for (auto &l : levels_)
			list_for_each(it, &l) f(container_of(it, task, run_node));
	}

      private:
	static int level(task *t) { return t->prio < 0 ? 0 : (t->prio > 31 ? 31 : t->prio); }
	void push(task *t, bool front)
	{
		int p = level(t);
		if (front)
			list_push_front(&levels_[p], &t->run_node);
		else
			list_push_back(&levels_[p], &t->run_node);
		bitmap_ |= 1u << p;
		count_++;
	}
	list_node levels_[32];
	u32 bitmap_ = 0;
	u32 count_ = 0;
};

class Policy {
      public:
	virtual void init() { q_.reset(); }
	virtual void setup(task *t) = 0;
	virtual void task_create(task *t)
	{
		setup(t);
		t->ticks_left = t->quantum_ticks;
		q_.push_back(t);
	}
	virtual void task_wake(task *t) { q_.push_back(t); }
	virtual void requeue(task *t)
	{
		if (!t->ticks_left)
			t->ticks_left = t->quantum_ticks;
		q_.push_back(t);
	}
	virtual void task_block(task *t) { t->ticks_left = t->quantum_ticks; }
	virtual void task_exit(task *t) { q_.remove(t); }
	virtual void remove(task *t) { q_.remove(t); }
	virtual bool tick(task *cur)
	{
		if (cur->ticks_left)
			cur->ticks_left--;
		if (cur->ticks_left == 0) {
			cur->ticks_left = cur->quantum_ticks;
			return q_.count() > 0;
		}
		return false;
	}
	virtual task *select_next() { return q_.pop(); }
	virtual void classified(task *) {}
	virtual u32 runnable() { return q_.count(); }

      protected:
	static u32 scaled(u32 ticks)
	{
		u32 q = ticks * (u32)quantum_scale() / 100;
		return q ? q : 1;
	}
	PrioQueue q_;
};

} // namespace fuhrer

#define FUHRER_EXPORT_POLICY(Class, cname, desc)                                              \
	static Class cname##_instance;                                                    \
	extern "C" const struct sched_policy cname##_policy = {                            \
		#cname,                                                                   \
		desc,                                                                     \
		[] { cname##_instance.init(); },                                          \
		[](task *t) { cname##_instance.setup(t); },                               \
		[](task *t) { cname##_instance.task_create(t); },                         \
		[](task *t) { cname##_instance.task_wake(t); },                           \
		[](task *t) { cname##_instance.requeue(t); },                             \
		[](task *t) { cname##_instance.task_block(t); },                          \
		[](task *t) { cname##_instance.task_exit(t); },                           \
		[](task *t) { cname##_instance.remove(t); },                              \
		[](task *t) { return cname##_instance.tick(t); },                         \
		[]() { return cname##_instance.select_next(); },                          \
		[](task *t) { cname##_instance.classified(t); },                          \
		[]() { return cname##_instance.runnable(); },                             \
	};
