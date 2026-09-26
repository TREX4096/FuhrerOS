#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "fuhrer/common.h"
#include "fuhrer/config.h"

static const char *mode_names[FU_MODE_COUNT] = {
	[FU_MODE_ADAPTIVE] = "adaptive",
	[FU_MODE_OBSERVE] = "observe",
	[FU_MODE_STATIC] = "static",
	[FU_MODE_OFF] = "off",
};

const char *fu_mode_name(enum fu_mode m)
{
	return (m >= 0 && m < FU_MODE_COUNT) ? mode_names[m] : "unknown";
}

int fu_mode_from_name(const char *s)
{
	for (int i = 0; i < FU_MODE_COUNT; i++)
		if (strcasecmp(s, mode_names[i]) == 0)
			return i;
	return -1;
}

void fu_config_default(struct fu_config *c)
{
	memset(c, 0, sizeof(*c));
	c->interval_ms = 1000;
	c->hysteresis = 3;
	c->min_dwell_ms = 3000;
	c->scan_procs = 1;
	c->mode = FU_MODE_ADAPTIVE;
	c->static_policy = FU_POLICY_NORMAL;
	/* Default class -> policy map (D-006). M5 static benchmarks are meant
	 * to confirm or overturn each row. */
	c->class_policy[FU_CLASS_IDLE] = FU_POLICY_NORMAL;
	c->class_policy[FU_CLASS_INTERACTIVE] = FU_POLICY_NORMAL;
	c->class_policy[FU_CLASS_CPU_BOUND] = FU_POLICY_BATCHED;
	c->class_policy[FU_CLASS_IO_SEQUENTIAL] = FU_POLICY_BATCHED;
	c->class_policy[FU_CLASS_IO_RANDOM] = FU_POLICY_SPECIALIZED;
	c->class_policy[FU_CLASS_NETWORK_HEAVY] = FU_POLICY_SPECIALIZED;
	c->class_policy[FU_CLASS_MIXED] = FU_POLICY_NORMAL;
	fu_thresholds_default(&c->th);
	snprintf(c->run_dir, sizeof(c->run_dir), "/run/fuhrer");
	snprintf(c->log_dir, sizeof(c->log_dir), "/var/log/fuhrer");
}

int fu_config_set_mode(struct fu_config *c, const char *spec)
{
	if (strncasecmp(spec, "static:", 7) == 0) {
		int p = fu_policy_from_name(spec + 7);
		if (p < 0)
			return -1;
		c->mode = FU_MODE_STATIC;
		c->static_policy = (enum fu_policy_id)p;
		return 0;
	}
	int m = fu_mode_from_name(spec);
	if (m < 0)
		return -1;
	c->mode = (enum fu_mode)m;
	return 0;
}

static int parse_int(const char *v, int lo, int hi, int *out)
{
	char *end;
	long x = strtol(v, &end, 10);
	if (end == v || *end || x < lo || x > hi)
		return -1;
	*out = (int)x;
	return 0;
}

static int parse_double(const char *v, double *out)
{
	char *end;
	double x = strtod(v, &end);
	if (end == v || *end || x < 0)
		return -1;
	*out = x;
	return 0;
}

int fu_config_set(struct fu_config *c, const char *key, const char *v)
{
	if (!strcmp(key, "interval_ms"))
		return parse_int(v, 10, 600000, &c->interval_ms);
	if (!strcmp(key, "hysteresis"))
		return parse_int(v, 1, 1000, &c->hysteresis);
	if (!strcmp(key, "min_dwell_ms"))
		return parse_int(v, 0, 3600000, &c->min_dwell_ms);
	if (!strcmp(key, "scan_procs"))
		return parse_int(v, 0, 1, &c->scan_procs);
	if (!strcmp(key, "dry_run"))
		return parse_int(v, 0, 1, &c->dry_run);
	if (!strcmp(key, "mode"))
		return fu_config_set_mode(c, v);
	if (!strcmp(key, "run_dir")) {
		snprintf(c->run_dir, sizeof(c->run_dir), "%s", v);
		return 0;
	}
	if (!strcmp(key, "log_dir")) {
		snprintf(c->log_dir, sizeof(c->log_dir), "%s", v);
		return 0;
	}
	if (!strncmp(key, "map.", 4)) {
		int cls = fu_class_from_name(key + 4);
		int pol = fu_policy_from_name(v);
		if (cls < 0 || pol < 0)
			return -1;
		c->class_policy[cls] = (enum fu_policy_id)pol;
		return 0;
	}
	struct { const char *k; double *p; } th[] = {
		{ "cpu_bound_pct", &c->th.cpu_bound_pct },
		{ "io_active_iops", &c->th.io_active_iops },
		{ "io_active_mbps", &c->th.io_active_mbps },
		{ "app_io_active_ops", &c->th.app_io_active_ops },
		{ "small_io_kb", &c->th.small_io_kb },
		{ "large_io_kb", &c->th.large_io_kb },
		{ "seq_merge_ratio", &c->th.seq_merge_ratio },
		{ "net_active_pps", &c->th.net_active_pps },
		{ "net_active_mbps", &c->th.net_active_mbps },
		{ "interactive_ctxsw", &c->th.interactive_ctxsw },
	};
	for (size_t i = 0; i < FU_ARRAY_LEN(th); i++)
		if (!strcmp(key, th[i].k))
			return parse_double(v, th[i].p);
	return -1;
}

static char *strip(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	return fu_rtrim(s);
}

int fu_config_parse(struct fu_config *c, char *text)
{
	int errors = 0, lineno = 0;
	char *save = NULL;
	for (char *line = strtok_r(text, "\n", &save); line;
	     line = strtok_r(NULL, "\n", &save)) {
		lineno++;
		char *hash = strchr(line, '#');
		if (hash)
			*hash = '\0';
		line = strip(line);
		if (!*line)
			continue;
		char *eq = strchr(line, '=');
		if (!eq) {
			FU_WARN("config line %d: expected key = value", lineno);
			errors++;
			continue;
		}
		*eq = '\0';
		char *key = strip(line), *val = strip(eq + 1);
		if (fu_config_set(c, key, val) < 0) {
			FU_WARN("config line %d: bad setting '%s = %s'", lineno, key, val);
			errors++;
		}
	}
	return errors;
}

int fu_config_load(struct fu_config *c, const char *path)
{
	char buf[16384];
	if (fu_read_file_abs(path, buf, sizeof(buf)) < 0)
		return 0;
	return fu_config_parse(c, buf);
}

void fu_config_dump(const struct fu_config *c, FILE *out)
{
	fprintf(out, "mode = %s%s%s\n", fu_mode_name(c->mode),
		c->mode == FU_MODE_STATIC ? ":" : "",
		c->mode == FU_MODE_STATIC ? fu_policy_name(c->static_policy) : "");
	fprintf(out, "interval_ms = %d\nhysteresis = %d\nmin_dwell_ms = %d\nscan_procs = %d\n",
		c->interval_ms, c->hysteresis, c->min_dwell_ms, c->scan_procs);
	for (int i = 0; i < FU_CLASS_COUNT; i++)
		fprintf(out, "map.%s = %s\n", fu_class_name(i), fu_policy_name(c->class_policy[i]));
	fprintf(out,
		"cpu_bound_pct = %g\nio_active_iops = %g\nio_active_mbps = %g\n"
		"app_io_active_ops = %g\nsmall_io_kb = %g\nlarge_io_kb = %g\n"
		"seq_merge_ratio = %g\nnet_active_pps = %g\nnet_active_mbps = %g\n"
		"interactive_ctxsw = %g\n",
		c->th.cpu_bound_pct, c->th.io_active_iops, c->th.io_active_mbps,
		c->th.app_io_active_ops, c->th.small_io_kb, c->th.large_io_kb,
		c->th.seq_merge_ratio, c->th.net_active_pps, c->th.net_active_mbps,
		c->th.interactive_ctxsw);
}
