/*
 * fuhrerd configuration (/etc/fuhrer/fuhrer.conf, key = value).
 */
#ifndef FUHRER_CONFIG_H
#define FUHRER_CONFIG_H

#include <stdio.h>

#include "fuhrer/features.h"
#include "fuhrer/policy.h"
#include "fuhrer/shm.h"

struct fu_config {
	int interval_ms;		/* sampling period (D-004) */
	int hysteresis;			/* consecutive agreeing samples before switching */
	int min_dwell_ms;		/* minimum time between switches */
	int scan_procs;			/* per-process attribution (PID in logs) */
	enum fu_mode mode;
	enum fu_policy_id static_policy;
	enum fu_policy_id class_policy[FU_CLASS_COUNT];
	struct fu_thresholds th;
	int dry_run;
	char run_dir[128];
	char log_dir[128];
};

void fu_config_default(struct fu_config *c);
/* Apply one "key = value" pair. Returns 0, or -1 for an unknown key/value. */
int fu_config_set(struct fu_config *c, const char *key, const char *value);
/* Parse a config file; missing file is not an error. Returns #errors. */
int fu_config_load(struct fu_config *c, const char *path);
/* Parse the whole text of a config (used by tests and the loader). */
int fu_config_parse(struct fu_config *c, char *text);
/* "static:batched", "adaptive", ... */
int fu_config_set_mode(struct fu_config *c, const char *spec);
void fu_config_dump(const struct fu_config *c, FILE *out);

#endif
