#define _GNU_SOURCE
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include "fuhrer/common.h"
#include "fuhrer/policy.h"
#include "fuhrer/profiler.h"

/*
 * Tunable table. Columns: NORMAL, BATCHED, SPECIALIZED. NULL keeps the
 * original value. Order matters: the scheduler is switched before
 * nr_requests because changing the elevator resets the queue depth.
 * See docs/decisions.md D-007 for why these particular knobs were chosen.
 */
struct knob_def {
	const char *path; /* "{dev}" expands to every physical disk */
	const char *vals[FU_POLICY_COUNT];
};

static const struct knob_def knob_table[] = {
	/* block layer */
	{ "/sys/block/{dev}/queue/scheduler",      { NULL, "mq-deadline", "none" } },
	{ "/sys/block/{dev}/queue/read_ahead_kb",  { NULL, "4096", "16" } },
	{ "/sys/block/{dev}/queue/nr_requests",    { NULL, "x2", NULL } },
	{ "/sys/block/{dev}/queue/nomerges",       { NULL, "0", "1" } },
	{ "/sys/block/{dev}/queue/rq_affinity",    { NULL, NULL, "2" } },
	/* page-cache writeback */
	{ "/proc/sys/vm/dirty_background_ratio",   { NULL, "20", "5" } },
	{ "/proc/sys/vm/dirty_ratio",              { NULL, "40", NULL } },
	{ "/proc/sys/vm/dirty_expire_centisecs",   { NULL, "6000", NULL } },
	{ "/proc/sys/vm/dirty_writeback_centisecs",{ NULL, "1500", NULL } },
	/* networking */
	{ "/proc/sys/net/core/busy_read",          { NULL, NULL, "50" } },
	{ "/proc/sys/net/core/busy_poll",          { NULL, NULL, "50" } },
	{ "/proc/sys/net/core/netdev_budget",      { NULL, "600", NULL } },
	{ "/proc/sys/net/core/netdev_budget_usecs",{ NULL, "8000", NULL } },
	/* CPU scheduler (EEVDF base slice; needs debugfs) */
	{ "/sys/kernel/debug/sched/base_slice_ns", { NULL, "x4", NULL } },
};

void fu_knob_normalize(const char *path, char *val)
{
	fu_rtrim(val);
	/* "mq-deadline [none]" -> "none" */
	if (strstr(path, "/scheduler")) {
		char *l = strchr(val, '['), *r = strchr(val, ']');
		if (l && r && r > l) {
			size_t n = (size_t)(r - l - 1);
			memmove(val, l + 1, n);
			val[n] = '\0';
		}
	}
}

int fu_knob_target(const struct fu_knob *k, enum fu_policy_id id, char *out, size_t len)
{
	const char *v = k->vals[id];
	if (!v) {
		snprintf(out, len, "%s", k->orig);
		return 0;
	}
	if (v[0] == 'x') {
		long long base = atoll(k->orig);
		long long mult = atoll(v + 1);
		if (base <= 0 || mult <= 0)
			return -1;
		snprintf(out, len, "%lld", base * mult);
		return 0;
	}
	snprintf(out, len, "%s", v);
	return 0;
}

static int add_knob(struct fu_policy_ctx *ctx, const char *path, const struct knob_def *def)
{
	if (ctx->nknobs >= FU_MAX_KNOBS)
		return -1;
	struct fu_knob *k = &ctx->knobs[ctx->nknobs++];
	memset(k, 0, sizeof(*k));
	snprintf(k->path, sizeof(k->path), "%s", path);
	memcpy(k->vals, def->vals, sizeof(k->vals));
	return 0;
}

static void load_state(struct fu_policy_ctx *ctx)
{
	char buf[8192];
	if (!ctx->state_file[0] || fu_read_file_abs(ctx->state_file, buf, sizeof(buf)) <= 0)
		return;
	char *save = NULL;
	for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *tab = strchr(line, '\t');
		if (!tab)
			continue;
		*tab = '\0';
		for (int i = 0; i < ctx->nknobs; i++) {
			if (strcmp(ctx->knobs[i].path, line) == 0 && ctx->knobs[i].available) {
				snprintf(ctx->knobs[i].orig, FU_KNOB_VAL_LEN, "%s", tab + 1);
				/* A previous instance may have left it modified. */
				ctx->knobs[i].changed = 1;
			}
		}
	}
}

static void save_state(struct fu_policy_ctx *ctx)
{
	if (!ctx->state_file[0])
		return;
	char buf[8192];
	size_t off = 0;
	for (int i = 0; i < ctx->nknobs && off < sizeof(buf); i++) {
		if (!ctx->knobs[i].available)
			continue;
		off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s\t%s\n",
					ctx->knobs[i].path, ctx->knobs[i].orig);
	}
	if (off > sizeof(buf))
		off = sizeof(buf);
	if (fu_write_atomic(ctx->state_file, buf, off) < 0)
		FU_WARN("cannot persist original knob values to %s", ctx->state_file);
}

int fu_knobs_init(struct fu_policy_ctx *ctx, const char *state_file, int dry_run)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->dry_run = dry_run;
	if (state_file)
		snprintf(ctx->state_file, sizeof(ctx->state_file), "%s", state_file);

	/* Enumerate physical disks once; hot-plugged disks are out of scope. */
	char disks[16][32];
	int ndisks = 0;
	char sysblock[300];
	snprintf(sysblock, sizeof(sysblock), "%s/sys/block", fu_sys_root);
	DIR *d = opendir(sysblock);
	if (d) {
		struct dirent *de;
		while ((de = readdir(d)) && ndisks < 16)
			if (fu_is_physical_disk(de->d_name))
				snprintf(disks[ndisks++], 32, "%s", de->d_name);
		closedir(d);
	}

	for (size_t i = 0; i < FU_ARRAY_LEN(knob_table); i++) {
		const struct knob_def *def = &knob_table[i];
		const char *dev = strstr(def->path, "{dev}");
		if (!dev) {
			add_knob(ctx, def->path, def);
			continue;
		}
		for (int j = 0; j < ndisks; j++) {
			char path[160];
			snprintf(path, sizeof(path), "%.*s%s%s", (int)(dev - def->path), def->path,
				 disks[j], dev + 5);
			add_knob(ctx, path, def);
		}
	}

	int avail = 0;
	for (int i = 0; i < ctx->nknobs; i++) {
		struct fu_knob *k = &ctx->knobs[i];
		char val[FU_KNOB_VAL_LEN];
		if (fu_read_file(k->path, val, sizeof(val)) > 0) {
			fu_knob_normalize(k->path, val);
			snprintf(k->orig, sizeof(k->orig), "%s", val);
			k->available = 1;
			avail++;
		}
	}
	load_state(ctx);
	save_state(ctx);
	for (int p = 0; p < FU_POLICY_COUNT; p++)
		ctx->metrics[p].knobs_total = avail;
	return avail;
}

static int write_knob(struct fu_policy_ctx *ctx, struct fu_knob *k, const char *val)
{
	if (ctx->dry_run) {
		FU_INFO("dry-run: %s <- %s", k->path, val);
		return 0;
	}
	int r = fu_write_file(k->path, val);
	if (r < 0)
		FU_WARN("knob %s <- %s failed: %s", k->path, val, strerror(-r));
	return r;
}

int fu_knobs_apply(struct fu_policy_ctx *ctx, enum fu_policy_id id)
{
	struct fu_policy_metrics *m = &ctx->metrics[id];
	double t0 = fu_now();
	int applied = 0, failed = 0;
	for (int i = 0; i < ctx->nknobs; i++) {
		struct fu_knob *k = &ctx->knobs[i];
		if (!k->available || !k->vals[id])
			continue;
		char val[FU_KNOB_VAL_LEN];
		if (fu_knob_target(k, id, val, sizeof(val)) < 0) {
			failed++;
			continue;
		}
		if (write_knob(ctx, k, val) == 0) {
			k->changed = strcmp(val, k->orig) != 0;
			applied++;
		} else {
			failed++;
		}
	}
	m->knobs_applied = applied;
	m->knobs_failed = failed;
	m->activations++;
	m->last_activate_ms = (fu_now() - t0) * 1000.0;
	return failed ? -1 : 0;
}

int fu_knobs_restore(struct fu_policy_ctx *ctx, enum fu_policy_id id)
{
	int failed = 0;
	for (int i = 0; i < ctx->nknobs; i++) {
		struct fu_knob *k = &ctx->knobs[i];
		if (!k->available || !k->changed)
			continue;
		if (id < FU_POLICY_COUNT && !k->vals[id])
			continue;
		if (write_knob(ctx, k, k->orig) == 0)
			k->changed = 0;
		else
			failed++;
	}
	return failed ? -1 : 0;
}

int fu_knobs_restore_all(struct fu_policy_ctx *ctx)
{
	return fu_knobs_restore(ctx, FU_POLICY_COUNT);
}
