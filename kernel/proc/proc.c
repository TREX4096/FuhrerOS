/* Processes: creation from ELF files, threads, exit, wait, kill, file
 * descriptors and safe user-memory access. */
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "kernel.h"
#include "mm.h"
#include "proc.h"
#include "sched.h"
#include "tty.h"
#include "vfs.h"

struct list_node all_procs = LIST_INIT(all_procs);
static pid_t next_pid = 1;

struct task *task_create_suspended(const char *name, void (*fn)(void *), void *arg);
void task_start(struct task *t);

struct process *proc_current(void)
{
	struct task *t = sched_current();
	return t ? t->proc : NULL;
}

struct process *proc_by_pid(pid_t pid)
{
	list_for_each(it, &all_procs) {
		struct process *p = container_of(it, struct process, all_node);
		if (p->pid == pid)
			return p;
	}
	return NULL;
}

int proc_fd_alloc(struct process *p, struct file *f)
{
	for (int i = 0; i < MAX_FDS; i++)
		if (!p->fds[i]) {
			p->fds[i] = f;
			return i;
		}
	return -E_MFILE;
}

struct file *proc_fd_get(struct process *p, int fd)
{
	if (fd < 0 || fd >= MAX_FDS)
		return NULL;
	return p->fds[fd];
}

/* ---- user memory ---- */
bool user_range_ok(const void *uptr, u64 len, bool write)
{
	u64 a = (u64)uptr;
	if (a + len < a || a + len > USER_TOP || a < 0x1000)
		return false;
	struct process *p = proc_current();
	if (!p)
		return false;
	for (u64 v = ALIGN_DOWN(a, PAGE_SIZE); v < a + len; v += PAGE_SIZE) {
		paddr_t pa = vmm_translate(p->as, v);
		if (!pa)
			return false;
		(void)write; /* all user mappings we create for data are writable */
	}
	return true;
}

int copy_from_user(void *dst, const void *usrc, u64 len)
{
	if (!user_range_ok(usrc, len, false))
		return -E_FAULT;
	memcpy(dst, usrc, len);
	return 0;
}

int copy_to_user(void *udst, const void *src, u64 len)
{
	if (!user_range_ok(udst, len, true))
		return -E_FAULT;
	memcpy(udst, src, len);
	return 0;
}

int strncpy_from_user(char *dst, const char *usrc, u64 max)
{
	for (u64 i = 0; i < max; i++) {
		if (!user_range_ok(usrc + i, 1, false))
			return -E_FAULT;
		dst[i] = usrc[i];
		if (!dst[i])
			return (int)i;
	}
	dst[max - 1] = 0;
	return -E_NAMETOOLONG;
}

/* ---- ELF64 loader ---- */
struct PACKED elf64_ehdr {
	u8 ident[16];
	u16 type, machine;
	u32 version;
	u64 entry, phoff, shoff;
	u32 flags;
	u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct PACKED elf64_phdr {
	u32 type, flags;
	u64 offset, vaddr, paddr, filesz, memsz, align;
};
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2

static int load_elf(struct address_space *as, const u8 *img, u64 len, u64 *entry, u64 *brk)
{
	const struct elf64_ehdr *eh = (const void *)img;
	if (len < sizeof(*eh) || memcmp(eh->ident, "\177ELF", 4) || eh->ident[4] != 2 ||
	    eh->machine != 62 || eh->type != 2)
		return -E_NOEXEC;
	if (eh->phoff + (u64)eh->phnum * sizeof(struct elf64_phdr) > len)
		return -E_NOEXEC;
	u64 top = 0;
	for (u16 i = 0; i < eh->phnum; i++) {
		const struct elf64_phdr *ph = (const void *)(img + eh->phoff + i * sizeof(*ph));
		if (ph->type != PT_LOAD || !ph->memsz)
			continue;
		if (ph->vaddr < 0x100000 || ph->vaddr + ph->memsz > USER_MMAP_BASE ||
		    ph->offset + ph->filesz > len || ph->filesz > ph->memsz)
			return -E_NOEXEC;
		u32 fl = VM_USER | VM_WRITE | ((ph->flags & PF_X) ? VM_EXEC : 0);
		u64 v0 = ALIGN_DOWN(ph->vaddr, PAGE_SIZE), v1 = ALIGN_UP(ph->vaddr + ph->memsz, PAGE_SIZE);
		for (u64 v = v0; v < v1; v += PAGE_SIZE) {
			paddr_t f = vmm_translate(as, v) & ~0xFFFULL;
			if (!f) {
				f = pmm_alloc_frame();
				if (!f || vmm_map_page(as, v, f, fl) < 0)
					return -E_NOMEM;
			} else if (ph->flags & PF_X) {
				vmm_protect_page(as, v, fl); /* page shared with data: keep exec */
			}
			/* copy the part of the file image that lands in this page */
			u64 seg_lo = MAX(v, ph->vaddr), seg_hi = MIN(v + PAGE_SIZE, ph->vaddr + ph->filesz);
			if (seg_lo < seg_hi)
				memcpy((u8 *)phys_to_virt(f) + (seg_lo - v),
				       img + ph->offset + (seg_lo - ph->vaddr), seg_hi - seg_lo);
		}
		if (v1 > top)
			top = v1;
	}
	*entry = eh->entry;
	*brk = top;
	return 0;
}

/* ---- process creation ---- */
struct user_start {
	u64 entry, stack, argc, argv;
};

static void user_thread_start(void *arg)
{
	struct user_start us = *(struct user_start *)arg;
	kfree(arg);
	enter_user(us.entry, us.stack, us.argc, us.argv);
}

/* Lay out argc/argv in a dedicated page: pointers first, then strings. */
static int setup_args(struct address_space *as, const char *const *argv, u64 *uargv, u64 *argc)
{
	u64 n = 0, bytes = 0;
	while (argv && argv[n] && n < 64) {
		bytes += strlen(argv[n]) + 1;
		n++;
	}
	u64 need = (n + 1) * 8 + bytes;
	u64 pages = ALIGN_UP(need, PAGE_SIZE) / PAGE_SIZE;
	if (pages > 16)
		return -E_RANGE;
	paddr_t frames[16];
	for (u64 i = 0; i < pages; i++) {
		frames[i] = pmm_alloc_frame();
		if (!frames[i] || vmm_map_page(as, USER_ARGS_BASE + i * PAGE_SIZE, frames[i],
					       VM_USER | VM_WRITE) < 0)
			return -E_NOMEM;
	}
	/* write through the HHDM (the new space is not active yet) */
	u8 *kbuf = kzalloc(pages * PAGE_SIZE);
	u64 *ptrs = (u64 *)kbuf;
	u64 soff = (n + 1) * 8;
	for (u64 i = 0; i < n; i++) {
		ptrs[i] = USER_ARGS_BASE + soff;
		u64 l = strlen(argv[i]) + 1;
		memcpy(kbuf + soff, argv[i], l);
		soff += l;
	}
	ptrs[n] = 0;
	for (u64 i = 0; i < pages; i++)
		memcpy(phys_to_virt(frames[i]), kbuf + i * PAGE_SIZE, PAGE_SIZE);
	kfree(kbuf);
	*uargv = USER_ARGS_BASE;
	*argc = n;
	return 0;
}

int proc_spawn(const char *path, const char *const *argv, struct file *const *stdio, const char *cwd)
{
	struct process *parent = proc_current();
	void *img;
	u64 len;
	char abs[PATH_MAX];
	int r = vfs_normalize(cwd ? cwd : (parent ? parent->cwd : "/"), path, abs, sizeof(abs));
	if (r < 0)
		return r;
	r = vfs_read_all(abs, &img, &len);
	if (r < 0)
		return r;
	struct process *p = kzalloc(sizeof(*p));
	struct address_space *as = vmm_create_user_space();
	if (!p || !as) {
		kfree(img);
		kfree(p);
		return -E_NOMEM;
	}
	p->as = as;
	u64 entry, brk;
	r = load_elf(as, img, len, &entry, &brk);
	kfree(img);
	if (r < 0) {
		vmm_destroy_user_space(as);
		kfree(p);
		return r;
	}
	/* user stack with a guard gap below */
	r = vmm_map_anon(as, USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE, USER_STACK_PAGES,
			 VM_USER | VM_WRITE);
	u64 uargv = 0, argc = 0;
	if (r == 0)
		r = setup_args(as, argv, &uargv, &argc);
	if (r < 0) {
		vmm_destroy_user_space(as);
		kfree(p);
		return r;
	}

	const char *base = strrchr(abs, '/');
	strlcpy(p->name, base ? base + 1 : abs, sizeof(p->name));
	p->parent = parent;
	p->heap_start = p->heap_end = ALIGN_UP(brk, PAGE_SIZE) + PAGE_SIZE;
	p->mmap_next = USER_MMAP_BASE;
	p->start_ns = time_ns();
	p->priority = 16;
	list_init(&p->threads);
	wq_init(&p->child_exit);
	strlcpy(p->cwd, cwd ? cwd : (parent ? parent->cwd : "/"), sizeof(p->cwd));
	for (int i = 0; i < 3; i++) {
		struct file *f = stdio ? stdio[i] : NULL;
		if (!f && parent)
			f = parent->fds[i];
		if (f)
			file_ref(f);
		else
			f = tty_open_console(i == 0 ? O_RDONLY : O_WRONLY);
		p->fds[i] = f;
	}

	struct user_start *us = kmalloc(sizeof(*us));
	us->entry = entry;
	us->stack = USER_STACK_TOP - 8; /* entry: rsp ≡ 8 (mod 16) like after a call */
	us->argc = argc;
	us->argv = uargv;
	struct task *t = task_create_suspended(p->name, user_thread_start, us);
	if (!t) {
		vmm_destroy_user_space(as);
		kfree(p);
		kfree(us);
		return -E_NOMEM;
	}
	t->proc = p;
	t->user = true;
	u64 f = irq_save();
	p->pid = next_pid++;
	list_push_back(&p->threads, &t->proc_node);
	p->live_threads = 1;
	list_push_back(&all_procs, &p->all_node);
	irq_restore(f);
	task_start(t);
	return p->pid;
}

/* Additional thread in the current process (shares the address space). */
int proc_thread_create(u64 entry, u64 stack, u64 arg)
{
	struct process *p = proc_current();
	if (!p || !user_range_ok((void *)(stack - 16), 16, true))
		return -E_INVAL;
	struct user_start *us = kmalloc(sizeof(*us));
	us->entry = entry;
	us->stack = stack & ~0xFULL;
	us->stack -= 8;
	us->argc = arg;
	us->argv = 0;
	struct task *t = task_create_suspended(p->name, user_thread_start, us);
	if (!t)
		return -E_NOMEM;
	t->proc = p;
	t->user = true;
	u64 f = irq_save();
	list_push_back(&p->threads, &t->proc_node);
	p->live_threads++;
	irq_restore(f);
	task_start(t);
	return t->tid;
}

/* Last thread gone: release resources, become a zombie for the parent. */
__attribute__((weak)) void compositor_process_exited(pid_t pid) {}
void port_destroy_owned(pid_t pid);

void proc_thread_exited(struct task *t)
{
	struct process *p = t->proc;
	if (--p->live_threads > 0)
		return;
	/* windows are mapped into this address space: release them first */
	compositor_process_exited(p->pid);
	port_destroy_owned(p->pid);
	for (int i = 0; i < MAX_FDS; i++) {
		if (p->fds[i]) {
			struct file *f = p->fds[i];
			p->fds[i] = NULL;
			file_close(f); /* may wake pipe readers; interrupts are off, fine */
		}
	}
	p->exit_code = t->exit_code;
	p->state = PROC_ZOMBIE;
	/* orphans are adopted by nobody: they are reaped on exit */
	list_for_each(it, &all_procs) {
		struct process *c = container_of(it, struct process, all_node);
		if (c->parent == p)
			c->parent = NULL;
	}
	if (p->parent)
		wq_wake_all(&p->parent->child_exit);
	/* The address space cannot be freed while we still run on it; the
	 * reaper (wait) destroys it. */
}

/* Every thread must have exited *and* switched away (its kernel stack is
 * freed in sched_finish_switch) before the process can be freed. */
static bool fully_dead(struct process *p)
{
	if (p->state != PROC_ZOMBIE)
		return false;
	list_for_each(it, &p->threads) {
		struct task *t = container_of(it, struct task, proc_node);
		if (t->state != TASK_ZOMBIE || t->kstack_base)
			return false;
	}
	return true;
}

static void reap(struct process *p)
{
	list_remove(&p->all_node);
	list_for_each_safe(it, tmp, &p->threads) {
		struct task *t = container_of(it, struct task, proc_node);
		list_remove(&t->proc_node);
		task_reap(t);
	}
	vmm_destroy_user_space(p->as);
	kfree(p);
}

int proc_wait(pid_t pid, int *status)
{
	struct process *self = proc_current();
	u64 f = irq_save();
	for (;;) {
		bool have_child = false;
		list_for_each(it, &all_procs) {
			struct process *c = container_of(it, struct process, all_node);
			if (c->parent != self || (pid > 0 && c->pid != pid))
				continue;
			have_child = true;
			if (fully_dead(c)) {
				int code = c->exit_code;
				pid_t cp = c->pid;
				reap(c);
				irq_restore(f);
				if (status)
					*status = code;
				return cp;
			}
		}
		if (!have_child) {
			irq_restore(f);
			return -E_CHILD;
		}
		bool exiting = false;
		list_for_each(it, &all_procs) {
			struct process *c = container_of(it, struct process, all_node);
			if (c->parent == self && c->state == PROC_ZOMBIE)
				exiting = true; /* last thread still switching out */
		}
		if (!self || exiting) { /* poll briefly */
			irq_restore(f);
			sleep_ms(10);
			f = irq_save();
			continue;
		}
		wq_wait(&self->child_exit, 0);
		if (self->killed) {
			irq_restore(f);
			return -E_INTR;
		}
	}
}

/* Reap zombies whose parent is gone (called periodically from kinit/init). */
void proc_reap_orphans(void)
{
	u64 f = irq_save();
	list_for_each_safe(it, tmp, &all_procs) {
		struct process *c = container_of(it, struct process, all_node);
		if (!c->parent && fully_dead(c))
			reap(c);
	}
	irq_restore(f);
}

int proc_kill(pid_t pid)
{
	u64 f = irq_save();
	struct process *p = proc_by_pid(pid);
	if (!p || p->state == PROC_ZOMBIE) {
		irq_restore(f);
		return -E_SRCH;
	}
	p->killed = true;
	/* Wake every thread so blocking syscalls notice; they exit on the
	 * way back to user mode (syscall_dispatch checks `killed`). */
	list_for_each(it, &p->threads) {
		struct task *t = container_of(it, struct task, proc_node);
		if (t->state == TASK_BLOCKED || t->state == TASK_SLEEPING)
			task_wake(t);
	}
	irq_restore(f);
	return 0;
}

NORETURN void proc_exit(int code)
{
	struct process *p = proc_current();
	/* Other threads are killed too: blocked ones are woken so their
	 * syscalls return, running ones stop at their next kernel entry
	 * (syscall return or interrupt, see proc_check_killed). */
	if (p && !p->killed)
		proc_kill(p->pid);
	task_exit(code);
}

/* Called on every return to ring 3 from an interrupt. */
void proc_check_killed(struct trap_frame *tf)
{
	struct process *p = proc_current();
	if (p && p->killed && (tf->cs & 3)) {
		sti();
		proc_exit(130);
	}
}

/* Exceptions in ring 3 kill the process instead of the kernel. */
bool proc_handle_user_fault(struct trap_frame *tf)
{
	struct process *p = proc_current();
	if (!p)
		return false;
	extern u64 read_cr2_export(void);
	printk("\033[1;31m[fault]\033[0m %s (pid %d): exception %lu at rip %p, addr %p, err 0x%lx"
	       " - process terminated\n",
	       p->name, p->pid, tf->vector, (void *)tf->rip,
	       tf->vector == 14 ? (void *)read_cr2_export() : NULL, tf->error);
	sti();
	proc_exit(128 + (int)tf->vector);
}

u64 read_cr2_export(void) { return read_cr2(); }

void proc_init(void) { KLOG("proc", "process subsystem ready (ELF64 loader, spawn/wait)"); }
