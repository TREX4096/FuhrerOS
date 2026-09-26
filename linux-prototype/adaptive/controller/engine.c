#include <string.h>

#include "fuhrer/engine.h"

void fu_engine_init(struct fu_engine *e, const struct fu_config *cfg,
		    enum fu_policy_id initial, double now)
{
	memset(e, 0, sizeof(*e));
	e->cfg = cfg;
	e->current = initial;
	e->candidate = initial;
	/* Allow an immediate first adaptation rather than waiting a dwell. */
	e->last_switch_t = now - (double)cfg->min_dwell_ms / 1000.0;
}

void fu_engine_step(struct fu_engine *e, enum fu_class cls, double now,
		    struct fu_decision *d)
{
	const struct fu_config *c = e->cfg;
	memset(d, 0, sizeof(*d));
	d->from = e->current;

	enum fu_policy_id wanted;
	switch (c->mode) {
	case FU_MODE_STATIC:
		wanted = c->static_policy;
		break;
	case FU_MODE_ADAPTIVE:
		wanted = c->class_policy[cls];
		break;
	default: /* observe / off never change policy away from NORMAL */
		wanted = FU_POLICY_NORMAL;
		break;
	}
	d->wanted = wanted;
	d->to = wanted;

	if (wanted == e->current) {
		e->candidate = wanted;
		e->streak = 0;
		return;
	}
	/* Operator-selected modes take effect immediately. */
	if (c->mode != FU_MODE_ADAPTIVE) {
		d->do_switch = 1;
		return;
	}
	if (wanted == e->candidate) {
		e->streak++;
	} else {
		e->candidate = wanted;
		e->streak = 1;
	}
	if (e->streak < c->hysteresis) {
		d->why_not = "hysteresis";
		return;
	}
	if ((now - e->last_switch_t) * 1000.0 < (double)c->min_dwell_ms) {
		d->why_not = "dwell";
		return;
	}
	d->do_switch = 1;
}

void fu_engine_commit(struct fu_engine *e, enum fu_policy_id to, double now)
{
	if (to != e->current)
		e->switches++;
	e->current = to;
	e->candidate = to;
	e->streak = 0;
	e->last_switch_t = now;
}
