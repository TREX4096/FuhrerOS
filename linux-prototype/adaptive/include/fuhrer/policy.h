/*
 * FuhrerOS policy framework (M4).
 *
 * A policy is a named, reversible bundle of kernel tunables plus a hint that
 * Fuhrer-aware applications (libfuhrer) use to pick an I/O backend:
 *
 *   NORMAL       stock Linux settings, synchronous syscalls
 *   BATCHED      throughput-oriented: large read-ahead, deferred writeback,
 *                bigger NAPI budget, longer scheduler slice, batched io_uring
 *   SPECIALIZED  latency-oriented: minimal read-ahead, no I/O scheduler,
 *                completion on submitting CPU, socket busy-polling,
 *                io_uring + O_DIRECT fast path in libfuhrer
 *
 * Every tunable is saved before first modification and restored on
 * deactivate(), so policies never persist beyond the daemon's lifetime.
 */
#ifndef FUHRER_POLICY_H
#define FUHRER_POLICY_H

#include <stddef.h>

enum fu_policy_id {
	FU_POLICY_NORMAL = 0,
	FU_POLICY_BATCHED,
	FU_POLICY_SPECIALIZED,
	FU_POLICY_COUNT
};

#define FU_MAX_KNOBS 64
#define FU_KNOB_VAL_LEN 128

struct fu_knob {
	char path[160];
	char orig[FU_KNOB_VAL_LEN];
	const char *vals[FU_POLICY_COUNT]; /* NULL = original; "xN" = orig * N */
	int available;
	int changed; /* currently differs from orig because of us */
};

struct fu_policy_metrics {
	double last_activate_ms;
	int knobs_total;
	int knobs_applied;
	int knobs_failed;
	unsigned long activations;
};

struct fu_policy_ctx {
	struct fu_knob knobs[FU_MAX_KNOBS];
	int nknobs;
	int dry_run;			/* log writes instead of performing them */
	char state_file[256];		/* persisted originals (survive restart) */
	struct fu_policy_metrics metrics[FU_POLICY_COUNT];
};

struct fu_policy {
	enum fu_policy_id id;
	const char *(*name)(void);
	int (*initialize)(struct fu_policy_ctx *ctx);
	int (*activate)(struct fu_policy_ctx *ctx);
	int (*deactivate)(struct fu_policy_ctx *ctx);
	void (*collect_metrics)(struct fu_policy_ctx *ctx, struct fu_policy_metrics *out);
};

extern const struct fu_policy fu_policy_normal;
extern const struct fu_policy fu_policy_batched;
extern const struct fu_policy fu_policy_specialized;

const struct fu_policy *fu_policy_get(enum fu_policy_id id);
const char *fu_policy_name(enum fu_policy_id id);
int fu_policy_from_name(const char *name); /* -1 if unknown */

/* Knob manager shared by all policies. */
int fu_knobs_init(struct fu_policy_ctx *ctx, const char *state_file, int dry_run);
int fu_knobs_apply(struct fu_policy_ctx *ctx, enum fu_policy_id id);
int fu_knobs_restore(struct fu_policy_ctx *ctx, enum fu_policy_id id);
int fu_knobs_restore_all(struct fu_policy_ctx *ctx);
/* Resolve the value a knob should take under a policy (exported for tests). */
int fu_knob_target(const struct fu_knob *k, enum fu_policy_id id, char *out, size_t len);
/* Extract the active entry from "[mq-deadline] none"-style sysfs values. */
void fu_knob_normalize(const char *path, char *val);

#endif
