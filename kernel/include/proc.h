/* Processes: an address space, file descriptors, a working directory and one
 * or more tasks (threads). FuhrerOS-native ABI: processes are created with
 * spawn(path, argv, fds) rather than fork/exec (NEW_EXPLANATION §18). */
#ifndef FUHRER_PROC_H
#define FUHRER_PROC_H

#include "list.h"
#include "mm.h"
#include "sched.h"
#include "types.h"

#define MAX_FDS 64
#define PATH_MAX 256
#define USER_STACK_TOP 0x00007FFFFFFFE000ULL
#define USER_STACK_PAGES 64		/* 256 KiB */
#define USER_MMAP_BASE 0x0000200000000000ULL
#define USER_ARGS_BASE 0x00007FFFF0000000ULL

struct file;

enum proc_state { PROC_ALIVE, PROC_ZOMBIE };

struct process {
	pid_t pid;
	char name[32];
	enum proc_state state;
	struct address_space *as;
	struct process *parent;
	struct list_node all_node;
	struct list_node threads;
	int live_threads;
	int exit_code;
	struct file *fds[MAX_FDS];
	char cwd[PATH_MAX];
	struct waitqueue child_exit;	/* parent sleeps here in wait() */
	vaddr_t mmap_next;
	vaddr_t heap_start, heap_end;	/* brk() region */
	u64 start_ns;
	u64 syscalls;
	int priority;
	bool killed;
};

extern struct list_node all_procs;

void proc_init(void);
struct process *proc_current(void);
/* Create a process running the ELF at path. fds[0..2] may name files to
 * install as stdin/stdout/stderr (NULL = inherit from the caller, or the
 * console for kernel-spawned processes). Returns pid or -errno. */
int proc_spawn(const char *path, const char *const *argv, struct file *const *stdio,
	       const char *cwd);
int proc_wait(pid_t pid, int *status);		/* pid -1 = any child */
int proc_kill(pid_t pid);
void proc_thread_exited(struct task *t);
NORETURN void proc_exit(int code);
struct process *proc_by_pid(pid_t pid);
int proc_fd_alloc(struct process *p, struct file *f);
struct file *proc_fd_get(struct process *p, int fd);

/* user memory access (validates the range is user, mapped, and writable) */
bool user_range_ok(const void *uptr, u64 len, bool write);
int copy_from_user(void *dst, const void *usrc, u64 len);
int copy_to_user(void *udst, const void *src, u64 len);
int strncpy_from_user(char *dst, const char *usrc, u64 max);

/* enter ring 3 (arch/x86_64/usermode.S) */
NORETURN void enter_user(u64 rip, u64 rsp, u64 arg0, u64 arg1);

#endif
