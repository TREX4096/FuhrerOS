/* Per-CPU area, reached through the GS base while in kernel mode.
 * Field offsets are used by syscall.S — keep them in sync. */
#ifndef ARCH_PERCPU_H
#define ARCH_PERCPU_H
#include "types.h"

struct task;

struct percpu {
	u64 kernel_rsp;		/* 0: top of the current task's kernel stack */
	u64 user_rsp;		/* 8: scratch for the syscall entry path */
	struct task *current;	/* 16 */
	u64 self;		/* 24 */
};

#define PERCPU_KERNEL_RSP 0
#define PERCPU_USER_RSP 8

extern struct percpu bsp_percpu;
void percpu_init(void);
#endif
