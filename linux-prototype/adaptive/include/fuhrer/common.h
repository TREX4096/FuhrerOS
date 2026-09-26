/*
 * FuhrerOS — shared helpers for the adaptive layer.
 */
#ifndef FUHRER_COMMON_H
#define FUHRER_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define FUHRER_VERSION "0.4.0"

#define FU_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* Directory prefix prepended to /proc and /sys paths. Empty in production;
 * unit tests point it at tests/fixtures so parsing is deterministic. */
extern char fu_sys_root[256];

static inline double fu_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static inline double fu_wallclock(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Read a whole (small) file below fu_sys_root into buf. Returns bytes read
 * or -1. buf is always NUL terminated on success. */
ssize_t fu_read_file(const char *path, char *buf, size_t len);

/* Same, but path is used verbatim (no fu_sys_root prefix). */
ssize_t fu_read_file_abs(const char *path, char *buf, size_t len);

/* Write a string to a sysfs/procfs file below fu_sys_root. Returns 0/-errno. */
int fu_write_file(const char *path, const char *value);

/* Atomically replace a file (write tmp + rename). */
int fu_write_atomic(const char *path, const char *data, size_t len);

/* Trim trailing whitespace in place. */
char *fu_rtrim(char *s);

void fu_log(const char *level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#define FU_INFO(...) fu_log("INFO", __VA_ARGS__)
#define FU_WARN(...) fu_log("WARN", __VA_ARGS__)
#define FU_ERR(...)  fu_log("ERROR", __VA_ARGS__)

#endif
