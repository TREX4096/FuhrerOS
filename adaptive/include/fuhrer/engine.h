/*
 * Adaptive policy engine (M6): class -> desired policy, with hysteresis so
 * that one noisy sample cannot cause a switch. Pure logic, no I/O, so it can
 * be unit-tested and replayed offline against recorded metrics.
 */
#ifndef FUHRER_ENGINE_H
#define FUHRER_ENGINE_H

#include "fuhrer/config.h"

struct fu_engine {
	const struct fu_config *cfg;
	enum fu_policy_id current;
	enum fu_policy_id candidate;
	int streak;			/* consecutive samples wanting candidate */
	double last_switch_t;
	unsigned long switches;
};

struct fu_decision {
	int do_switch;
	enum fu_policy_id from, to, wanted;
	const char *why_not;		/* "hysteresis", "dwell", "mode", NULL */
};

void fu_engine_init(struct fu_engine *e, const struct fu_config *cfg,
		    enum fu_policy_id initial, double now);
/* Feed one classification. Caller performs the switch if do_switch. */
void fu_engine_step(struct fu_engine *e, enum fu_class cls, double now,
		    struct fu_decision *d);
/* Record that a switch actually happened (after activation succeeded). */
void fu_engine_commit(struct fu_engine *e, enum fu_policy_id to, double now);

#endif
