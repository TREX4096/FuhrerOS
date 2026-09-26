/* System call dispatch (FuhrerOS native ABI, include/uapi/fuhrer.h). */
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/percpu.h"
#include "kernel.h"
#include "mm.h"
#include "proc.h"
#include "sched.h"
#include "tty.h"
#include "vfs.h"

extern char syscall_entry[];
int pipe_create(struct file **rd, struct file **wr);
int port_create(const char *name);
int port_lookup(const char *name);
int port_send(int id, const void *data, u32 len);
int port_recv(int id, void *data, u32 max, pid_t *sender, u64 timeout_ms);
int proc_thread_create(u64 entry, u64 stack, u64 arg);
void profiler_set_hysteresis(u32 h);
u64 tsc_frequency(void);

/* Subsystems that register later provide these (weak = not built yet). */
__attribute__((weak)) i64 sys_net(u64 nr, u64 a, u64 b, u64 c, u64 d, u64 e) { return -E_NOSYS; }
__attribute__((weak)) i64 sys_win(u64 nr, u64 a, u64 b, u64 c, u64 d, u64 e) { return -E_NOSYS; }
__attribute__((weak)) i64 sys_blk(u64 nr, u64 a, u64 b, u64 c, u64 d, u64 e) { return -E_NOSYS; }

static u64 syscall_counts[SYS_MAX];

void syscall_init(void)
{
	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1); /* SCE */
	wrmsr(MSR_STAR, ((u64)0x10 << 48) | ((u64)GDT_KCODE << 32));
	wrmsr(MSR_LSTAR, (u64)syscall_entry);
	wrmsr(MSR_SFMASK, 0x47700); /* IF, DF, TF, AC, NT cleared on entry */
	KLOG("syscall", "syscall/sysret MSRs programmed, %d syscall numbers", SYS_MAX);
}

static struct file *getf(int fd) { return proc_fd_get(proc_current(), fd); }

static i64 path_arg(const char *upath, char *out)
{
	int r = strncpy_from_user(out, upath, PATH_MAX);
	return r < 0 ? r : 0;
}

static bool io_like(struct file *f)
{
	int t = f->vn->type;
	return t == VT_FILE || t == VT_BLOCK || t == VT_SOCK || t == VT_CHAR;
}

static i64 sys_read(int fd, void *ubuf, u64 len)
{
	struct file *f = getf(fd);
	if (!f)
		return -E_BADF;
	if (!user_range_ok(ubuf, len, true))
		return -E_FAULT;
	if (io_like(f))
		sched_account_io();
	return file_read(f, ubuf, len);
}

static i64 sys_write(int fd, const void *ubuf, u64 len)
{
	struct file *f = getf(fd);
	if (!f)
		return -E_BADF;
	if (!user_range_ok(ubuf, len, false))
		return -E_FAULT;
	if (io_like(f) && f->vn->type != VT_CHAR)
		sched_account_io();
	return file_write(f, ubuf, len);
}

static i64 sys_open(const char *upath, int flags)
{
	char path[PATH_MAX];
	i64 r = path_arg(upath, path);
	if (r < 0)
		return r;
	struct process *p = proc_current();
	struct file *f;
	r = vfs_open(p->cwd, path, flags, &f);
	if (r < 0)
		return r;
	int fd = proc_fd_alloc(p, f);
	if (fd < 0)
		file_close(f);
	return fd;
}

static i64 sys_close(int fd)
{
	struct process *p = proc_current();
	struct file *f = proc_fd_get(p, fd);
	if (!f)
		return -E_BADF;
	p->fds[fd] = NULL;
	file_close(f);
	return 0;
}

static i64 sys_mmap(u64 hint, u64 len, u64 prot)
{
	struct process *p = proc_current();
	u64 pages = ALIGN_UP(len, PAGE_SIZE) / PAGE_SIZE;
	if (!pages || pages > (1u << 20))
		return -E_INVAL;
	u64 fl = irq_save();
	u64 va = p->mmap_next;
	p->mmap_next += (pages + 1) * PAGE_SIZE; /* guard page between mappings */
	irq_restore(fl);
	u32 flags = VM_USER | VM_WRITE | ((prot & PROT_EXEC) ? VM_EXEC : 0);
	if (vmm_map_anon(p->as, va, pages, flags) < 0)
		return -E_NOMEM;
	return (i64)va;
}

static i64 sys_munmap(u64 addr, u64 len)
{
	struct process *p = proc_current();
	if (addr < USER_MMAP_BASE || addr + len > USER_ARGS_BASE)
		return -E_INVAL;
	for (u64 v = ALIGN_DOWN(addr, PAGE_SIZE); v < addr + len; v += PAGE_SIZE)
		vmm_unmap_page(p->as, v, true);
	return 0;
}

static i64 sys_brk(u64 newbrk)
{
	struct process *p = proc_current();
	if (newbrk == 0 || newbrk < p->heap_start)
		return (i64)p->heap_end;
	u64 cur = ALIGN_UP(p->heap_end, PAGE_SIZE), want = ALIGN_UP(newbrk, PAGE_SIZE);
	if (want > p->heap_start + (1ULL << 32))
		return -E_NOMEM;
	for (u64 v = cur; v < want; v += PAGE_SIZE)
		if (vmm_map_anon(p->as, v, 1, VM_USER | VM_WRITE) < 0)
			return -E_NOMEM;
	p->heap_end = newbrk;
	return (i64)p->heap_end;
}

static i64 sys_spawn(const char *upath, const char *const *uargv, const int *ustdio)
{
	char path[PATH_MAX];
	i64 r = path_arg(upath, path);
	if (r < 0)
		return r;
	/* Copy argv into kernel memory. */
	char *args[33];
	int n = 0;
	char *store = kmalloc(8192);
	u64 used = 0;
	while (uargv && n < 32) {
		const char *ua;
		if (copy_from_user(&ua, &uargv[n], sizeof(ua)) < 0) {
			kfree(store);
			return -E_FAULT;
		}
		if (!ua)
			break;
		int l = strncpy_from_user(store + used, ua, 8192 - used);
		if (l < 0) {
			kfree(store);
			return l;
		}
		args[n++] = store + used;
		used += (u64)l + 1;
	}
	args[n] = NULL;
	struct file *stdio[3] = { NULL, NULL, NULL };
	if (ustdio) {
		int fds[3];
		if (copy_from_user(fds, ustdio, sizeof(fds)) < 0) {
			kfree(store);
			return -E_FAULT;
		}
		for (int i = 0; i < 3; i++)
			stdio[i] = fds[i] >= 0 ? getf(fds[i]) : NULL;
	}
	r = proc_spawn(path, (const char *const *)args, stdio, NULL);
	kfree(store);
	return r;
}

static i64 sys_sysinfo(struct fu_sysinfo *u)
{
	struct fu_sysinfo si;
	memset(&si, 0, sizeof(si));
	struct pmm_stats ps;
	struct sched_stats ss;
	pmm_get_stats(&ps);
	sched_get_stats(&ss);
	si.mem_total_kb = ps.total_frames * 4;
	si.mem_free_kb = ps.free_frames * 4;
	si.uptime_ns = time_ns();
	si.busy_ns = ss.busy_ns;
	si.idle_ns = ss.idle_ns;
	si.context_switches = ss.context_switches;
	si.tasks = ss.tasks;
	si.runnable = ss.runnable;
	si.cpus = 1;
	list_for_each(it, &all_procs) si.procs++;
	strlcpy(si.policy, sched_policy_name(), sizeof(si.policy));
	u8 conf;
	enum task_class c = profiler_system_class(&conf, NULL);
	strlcpy(si.system_class, task_class_name(c), sizeof(si.system_class));
	si.class_confidence = conf;
	strlcpy(si.cpu_brand, boot.cpu_brand, sizeof(si.cpu_brand));
	si.tsc_hz = tsc_frequency();
	return copy_to_user(u, &si, sizeof(si));
}

static i64 sys_sched_ctl(u64 op, u64 arg, char *ubuf)
{
	char name[32];
	switch (op) {
	case SCHED_SET_POLICY:
		if (strncpy_from_user(name, ubuf, sizeof(name)) < 0)
			return -E_FAULT;
		return sched_set_policy(name);
	case SCHED_GET_POLICY: {
		const char *n = sched_policy_name();
		return copy_to_user(ubuf, n, strlen(n) + 1);
	}
	case SCHED_SET_PRIORITY: {
		struct process *p = proc_by_pid((pid_t)(arg >> 8));
		if (!p)
			return -E_SRCH;
		p->priority = (int)(arg & 0xFF);
		list_for_each(it, &p->threads)
			task_set_priority(container_of(it, struct task, proc_node), p->priority);
		return 0;
	}
	case SCHED_SET_WINDOW: profiler_set_window_ms((u32)arg); return 0;
	case SCHED_SET_HYSTERESIS: profiler_set_hysteresis((u32)arg); return 0;
	case SCHED_PROFILER: profiler_enable(arg != 0); return 0;
	case SCHED_QUANTUM_SCALE: sched_set_quantum_scale((int)arg); return 0;
	case SCHED_TASK_CLASS: {
		struct task *t = task_by_tid((int)arg);
		return t ? (i64)t->prof.cls : -E_SRCH;
	}
	}
	return -E_INVAL;
}

static i64 sys_poll(int *ufds, int n, u64 timeout_ms)
{
	/* Level-triggered readiness over up to 16 fds; returns a bitmask of
	 * readable fds (by index). Sleeps in 5 ms steps until ready/timeout. */
	int fds[16];
	if (n < 1 || n > 16 || copy_from_user(fds, ufds, (u64)n * sizeof(int)) < 0)
		return -E_INVAL;
	u64 deadline = time_ns() + timeout_ms * 1000000ULL;
	for (;;) {
		i64 mask = 0;
		for (int i = 0; i < n; i++) {
			struct file *f = getf(fds[i]);
			if (f && f->vn->ops->poll && (f->vn->ops->poll(f->vn, 1) & 1))
				mask |= 1LL << i;
		}
		if (mask || (timeout_ms && time_ns() >= deadline))
			return mask;
		if (proc_current()->killed)
			return -E_INTR;
		sleep_ms(5);
	}
}

i64 syscall_dispatch_nr(u64 nr, u64 a, u64 b, u64 c, u64 d, u64 e)
{
	struct process *p = proc_current();
	if (nr < SYS_MAX)
		syscall_counts[nr]++;
	p->syscalls++;
	switch (nr) {
	case SYS_EXIT: proc_exit((int)a);
	case SYS_THREAD_EXIT: task_exit((int)a);
	case SYS_WRITE: return sys_write((int)a, (const void *)b, c);
	case SYS_READ: return sys_read((int)a, (void *)b, c);
	case SYS_OPEN: return sys_open((const char *)a, (int)b);
	case SYS_CLOSE: return sys_close((int)a);
	case SYS_YIELD: yield(); return 0;
	case SYS_SLEEP: sleep_ms(a); return 0;
	case SYS_MMAP: return sys_mmap(a, b, c);
	case SYS_MUNMAP: return sys_munmap(a, b);
	case SYS_BRK: return sys_brk(a);
	case SYS_SPAWN: return sys_spawn((const char *)a, (const char *const *)b, (const int *)c);
	case SYS_WAIT: {
		int st = 0;
		i64 r = proc_wait((pid_t)a, &st);
		if (r >= 0 && b && copy_to_user((void *)b, &st, sizeof(st)) < 0)
			return -E_FAULT;
		return r;
	}
	case SYS_GETPID: return p->pid;
	case SYS_GETPPID: return p->parent ? p->parent->pid : 0;
	case SYS_TIME: {
		struct fu_time t = { time_ns(), time_unix() };
		return copy_to_user((void *)a, &t, sizeof(t));
	}
	case SYS_STAT:
	case SYS_FSTAT: {
		struct stat st;
		if (nr == SYS_STAT) {
			char path[PATH_MAX];
			i64 r = path_arg((const char *)a, path);
			if (r < 0 || (r = vfs_stat(p->cwd, path, &st)) < 0)
				return r;
		} else {
			struct file *f = getf((int)a);
			if (!f)
				return -E_BADF;
			vnode_stat(f->vn, &st);
		}
		return copy_to_user((void *)b, &st, sizeof(st));
	}
	case SYS_READDIR: {
		struct file *f = getf((int)a);
		if (!f)
			return -E_BADF;
		struct dirent de;
		int r = file_readdir(f, b, &de);
		if (r > 0 && copy_to_user((void *)c, &de, sizeof(de)) < 0)
			return -E_FAULT;
		return r;
	}
	case SYS_MKDIR:
	case SYS_UNLINK:
	case SYS_CHDIR:
	case SYS_TRUNCATE: {
		char path[PATH_MAX];
		i64 r = path_arg((const char *)a, path);
		if (r < 0)
			return r;
		if (nr == SYS_MKDIR)
			return vfs_mkdir(p->cwd, path);
		if (nr == SYS_UNLINK)
			return vfs_unlink(p->cwd, path);
		if (nr == SYS_TRUNCATE)
			return vfs_truncate(p->cwd, path, b);
		struct stat st;
		char abs[PATH_MAX];
		if ((r = vfs_normalize(p->cwd, path, abs, sizeof(abs))) < 0)
			return r;
		if ((r = vfs_stat("/", abs, &st)) < 0)
			return r;
		if (st.type != VT_DIR)
			return -E_NOTDIR;
		strlcpy(p->cwd, abs, sizeof(p->cwd));
		return 0;
	}
	case SYS_RENAME: {
		char from[PATH_MAX], to[PATH_MAX];
		i64 r;
		if ((r = path_arg((const char *)a, from)) < 0 || (r = path_arg((const char *)b, to)) < 0)
			return r;
		return vfs_rename(p->cwd, from, to);
	}
	case SYS_GETCWD:
		if (strlen(p->cwd) + 1 > b)
			return -E_RANGE;
		return copy_to_user((void *)a, p->cwd, strlen(p->cwd) + 1);
	case SYS_PIPE: {
		struct file *rd, *wr;
		i64 r = pipe_create(&rd, &wr);
		if (r < 0)
			return r;
		int fds[2] = { proc_fd_alloc(p, rd), proc_fd_alloc(p, wr) };
		if (fds[0] < 0 || fds[1] < 0)
			return -E_MFILE;
		return copy_to_user((void *)a, fds, sizeof(fds));
	}
	case SYS_DUP2: {
		struct file *f = getf((int)a);
		if (!f || b >= MAX_FDS)
			return -E_BADF;
		if (a == b)
			return (i64)b;
		if (p->fds[b])
			file_close(p->fds[b]);
		file_ref(f);
		p->fds[b] = f;
		return (i64)b;
	}
	case SYS_KILL: return proc_kill((pid_t)a);
	case SYS_SYSINFO: return sys_sysinfo((struct fu_sysinfo *)a);
	case SYS_SCHED_CTL: return sys_sched_ctl(a, b, (char *)c);
	case SYS_SEEK: {
		struct file *f = getf((int)a);
		if (!f)
			return -E_BADF;
		i64 off = (i64)b;
		if (c == SEEK_CUR)
			off += (i64)f->off;
		else if (c == SEEK_END)
			off += (i64)f->vn->size;
		if (off < 0)
			return -E_INVAL;
		f->off = (u64)off;
		return off;
	}
	case SYS_IOCTL: {
		struct file *f = getf((int)a);
		if (!f)
			return -E_BADF;
		return f->vn->ops->ioctl ? f->vn->ops->ioctl(f->vn, b, c) : -E_INVAL;
	}
	case SYS_THREAD_CREATE: return proc_thread_create(a, b, c);
	case SYS_SYNC: vfs_sync_all(); return 0;
	case SYS_POLL: return sys_poll((int *)a, (int)b, c);
	case SYS_PORT_CREATE:
	case SYS_PORT_LOOKUP: {
		char name[32];
		if (strncpy_from_user(name, (const char *)a, sizeof(name)) < 0)
			return -E_FAULT;
		return nr == SYS_PORT_CREATE ? port_create(name) : port_lookup(name);
	}
	case SYS_PORT_SEND: {
		u8 msg[256];
		if (c > sizeof(msg) || copy_from_user(msg, (void *)b, c) < 0)
			return -E_FAULT;
		return port_send((int)a, msg, (u32)c);
	}
	case SYS_PORT_RECV: {
		u8 msg[256];
		pid_t sender = 0;
		int n = port_recv((int)a, msg, (u32)MIN(c, sizeof(msg)), &sender, e);
		if (n < 0)
			return n;
		if (copy_to_user((void *)b, msg, (u64)n) < 0 ||
		    (d && copy_to_user((void *)d, &sender, sizeof(sender)) < 0))
			return -E_FAULT;
		return n;
	}
	}
	if (nr >= SYS_SOCKET && nr <= SYS_NETINFO)
		return sys_net(nr, a, b, c, d, e);
	if (nr == SYS_DESKTOP_CTL && (a == 0x7E57 || a == 0x7E58) && boot_cmdline_has("usertest")) {
		/* End of the user-space test run (init): report and power off. */
		extern void selftest_summary(void);
		extern int selftest_failures(void);
		selftest_report("user.suite", a == 0x7E57, "usertest exit status %s",
				a == 0x7E57 ? "0" : "non-zero");
		selftest_summary();
		qemu_exit(selftest_failures() ? 2 : 1);
	}
	if (nr == SYS_DESKTOP_CTL && a == 99) { /* poweroff */
		printk("FuhrerOS: syncing filesystems and powering off\n");
		vfs_sync_all();
		printk("POWEROFF\n");
		qemu_exit(1); /* QEMU isa-debug-exit; on real hardware this port is ignored */
	}
	if (nr >= SYS_WIN_CREATE && nr <= SYS_DESKTOP_CTL)
		return sys_win(nr, a, b, c, d, e);
	if (nr == SYS_BLKSTAT || nr == SYS_CACHE_CTL)
		return sys_blk(nr, a, b, c, d, e);
	return -E_NOSYS;
}

void syscall_dispatch(struct trap_frame *tf)
{
	tf->rax = (u64)syscall_dispatch_nr(tf->rax, tf->rdi, tf->rsi, tf->rdx, tf->r10, tf->r8);
	struct process *p = proc_current();
	if (p && p->killed)
		proc_exit(130); /* killed while in the kernel */
}

u64 syscall_count(u32 nr) { return nr < SYS_MAX ? syscall_counts[nr] : 0; }
