/*
 * State published by fuhrerd for Fuhrer-aware applications.
 *
 * A small file in /run/fuhrer is mmap'ed read-only by libfuhrer. Writes use a
 * sequence lock (odd = update in progress) so readers never see a torn record
 * and never need a syscall to learn the current policy.
 */
#ifndef FUHRER_SHM_H
#define FUHRER_SHM_H

#include <stdint.h>

#define FU_SHM_PATH "/run/fuhrer/state.shm"
#define FU_SHM_MAGIC 0x46554852u /* "FUHR" */
#define FU_SHM_VERSION 1

enum fu_mode {
	FU_MODE_ADAPTIVE = 0,	/* B2: profile + classify + switch */
	FU_MODE_OBSERVE,	/* A2: profile + classify, never switch */
	FU_MODE_STATIC,		/* B1/B3/A3: fixed policy */
	FU_MODE_OFF,		/* A1/B0: no profiling, stock settings */
	FU_MODE_COUNT
};

struct fu_shared_state {
	uint32_t magic;
	uint32_t version;
	uint64_t seq;
	int32_t policy;		/* enum fu_policy_id */
	int32_t cls;		/* enum fu_class */
	int32_t mode;		/* enum fu_mode */
	int32_t pad;
	uint64_t switches;
	double policy_since;	/* CLOCK_MONOTONIC seconds */
};

const char *fu_mode_name(enum fu_mode m);
int fu_mode_from_name(const char *s);

#endif
