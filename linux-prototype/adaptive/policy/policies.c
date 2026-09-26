#include <string.h>
#include <strings.h>

#include "fuhrer/policy.h"

/*
 * The three initial policies (INSTRUCTION §11). They share the knob manager
 * and differ only in which column of the knob table they activate, which
 * keeps switching symmetric and every change reversible.
 */

#define DEFINE_POLICY(ident, ID, label)                                            \
	static const char *ident##_name(void) { return label; }                   \
	static int ident##_initialize(struct fu_policy_ctx *ctx)                   \
	{                                                                          \
		(void)ctx;                                                         \
		return 0;                                                          \
	}                                                                          \
	static int ident##_activate(struct fu_policy_ctx *ctx)                     \
	{                                                                          \
		return fu_knobs_apply(ctx, ID);                                    \
	}                                                                          \
	static int ident##_deactivate(struct fu_policy_ctx *ctx)                   \
	{                                                                          \
		return fu_knobs_restore(ctx, ID);                                  \
	}                                                                          \
	static void ident##_collect(struct fu_policy_ctx *ctx,                     \
				    struct fu_policy_metrics *out)                 \
	{                                                                          \
		*out = ctx->metrics[ID];                                           \
	}                                                                          \
	const struct fu_policy fu_policy_##ident = {                               \
		.id = ID,                                                          \
		.name = ident##_name,                                              \
		.initialize = ident##_initialize,                                  \
		.activate = ident##_activate,                                      \
		.deactivate = ident##_deactivate,                                  \
		.collect_metrics = ident##_collect,                                \
	};

DEFINE_POLICY(normal, FU_POLICY_NORMAL, "NORMAL")
DEFINE_POLICY(batched, FU_POLICY_BATCHED, "BATCHED")
DEFINE_POLICY(specialized, FU_POLICY_SPECIALIZED, "SPECIALIZED")

static const struct fu_policy *const all_policies[FU_POLICY_COUNT] = {
	&fu_policy_normal, &fu_policy_batched, &fu_policy_specialized,
};

const struct fu_policy *fu_policy_get(enum fu_policy_id id)
{
	return (id >= 0 && id < FU_POLICY_COUNT) ? all_policies[id] : NULL;
}

const char *fu_policy_name(enum fu_policy_id id)
{
	const struct fu_policy *p = fu_policy_get(id);
	return p ? p->name() : "UNKNOWN";
}

int fu_policy_from_name(const char *name)
{
	for (int i = 0; i < FU_POLICY_COUNT; i++)
		if (strcasecmp(name, all_policies[i]->name()) == 0)
			return i;
	/* short aliases used on the command line */
	if (strcasecmp(name, "fast") == 0 || strcasecmp(name, "spec") == 0)
		return FU_POLICY_SPECIALIZED;
	if (strcasecmp(name, "batch") == 0)
		return FU_POLICY_BATCHED;
	return -1;
}
