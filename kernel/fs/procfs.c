/* /proc: kernel state rendered as text on every read. User-space tools (ps,
 * top, the Control Center) parse these instead of needing special syscalls. */
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "kernel.h"
#include "mm.h"
#include "proc.h"
#include "sched.h"
#include "vfs.h"

struct pbuf {
	char *b;
	u64 len, cap;
};

__attribute__((format(printf, 2, 3))) void pb_printf(struct pbuf *p, const char *fmt, ...)
{
	if (p->len >= p->cap)
		return;
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(p->b + p->len, p->cap - p->len, fmt, ap);
	va_end(ap);
	p->len += (u64)n;
	if (p->len > p->cap)
		p->len = p->cap;
}

u64 tsc_frequency(void);
u32 timer_frequency(void);
size_t klog_read(char *buf, size_t len, u64 offset);
u64 klog_size(void);
u64 profiler_overhead_cycles(void);
u64 profiler_windows(void);
u32 profiler_hysteresis(void);

static void gen_uptime(struct pbuf *p)
{
	u64 ns = time_ns();
	pb_printf(p, "%lu.%03lu\n", ns / 1000000000UL, (ns / 1000000UL) % 1000);
}

static void gen_meminfo(struct pbuf *p)
{
	struct pmm_stats s;
	struct heap_stats h;
	pmm_get_stats(&s);
	heap_get_stats(&h);
	pb_printf(p, "MemTotal: %lu kB\nMemFree: %lu kB\nMemUsed: %lu kB\n", s.total_frames * 4,
		  s.free_frames * 4, s.used_frames * 4);
	pb_printf(p, "FrameAllocs: %lu\nFrameFrees: %lu\n", s.allocs, s.frees);
	pb_printf(p, "HeapSlabPages: %lu\nHeapLargePages: %lu\nHeapLiveSlabBytes: %lu\n", h.slab_pages,
		  h.large_pages, h.bytes_slab);
	pb_printf(p, "HeapAllocs: %lu\nHeapFrees: %lu\n", h.allocs, h.frees);
}

static void gen_cpuinfo(struct pbuf *p)
{
	pb_printf(p, "vendor: %s\nmodel: %s\ncpus_detected: %u\ncpus_used: 1\n", boot.cpu_vendor,
		  boot.cpu_brand, acpi.cpu_count);
	pb_printf(p, "tsc_mhz: %lu\ntimer_hz: %u\n", tsc_frequency() / 1000000, timer_frequency());
}

static const char *state_name(enum task_state s)
{
	static const char *n[] = { "R", "run", "blk", "sleep", "zomb" };
	return n[s];
}

static void gen_tasks(struct pbuf *p)
{
	pb_printf(p, "%-5s %-5s %-16s %-6s %-5s %-4s %-11s %4s %8s %8s %6s\n", "TID", "PID", "NAME",
		  "STATE", "PRIO", "QMS", "CLASS", "CONF", "CPU_MS", "SWITCH", "CPU%");
	u64 f = irq_save();
	list_for_each(it, &all_tasks) {
		struct task *t = container_of(it, struct task, all_node);
		pb_printf(p, "%-5d %-5d %-16s %-6s %-5d %-4u %-11s %3u%% %8lu %8lu %3lu.%lu\n", t->tid,
			  t->proc ? t->proc->pid : 0, t->name,
			  t == sched_current() ? "run" : state_name(t->state), t->prio,
			  t->quantum_ticks, task_class_name(t->prof.cls), t->prof.confidence,
			  t->cpu_ns / 1000000, t->switches, t->prof.cpu_share_pm / 10,
			  t->prof.cpu_share_pm % 10);
	}
	irq_restore(f);
}

static void gen_sched(struct pbuf *p)
{
	struct sched_stats s;
	sched_get_stats(&s);
	u64 hz = tsc_frequency();
	pb_printf(p, "policy: %s\n", sched_policy_name());
	pb_printf(p, "policies:");
	for (u32 i = 0; i < sched_policy_count; i++)
		pb_printf(p, " %s", sched_policies[i]->name);
	pb_printf(p, "\ncontext_switches: %lu\npreemptions: %lu\nsched_calls: %lu\n",
		  s.context_switches, s.preemptions, s.sched_calls);
	pb_printf(p, "sched_overhead_ns: %lu\n", hz ? s.sched_cycles * 1000000000UL / hz : 0);
	pb_printf(p, "busy_ns: %lu\nidle_ns: %lu\n", s.busy_ns, s.idle_ns);
	pb_printf(p, "tasks: %u\nrunnable: %u\npolicy_switches: %lu\n", s.tasks, s.runnable,
		  s.policy_switches);
	pb_printf(p, "profiler: %s\nwindow_ms: %u\nhysteresis: %u\nprofiler_windows: %lu\n",
		  profiler_enabled() ? "on" : "off", profiler_window_ms(), profiler_hysteresis(),
		  profiler_windows());
	pb_printf(p, "profiler_overhead_ns: %lu\n",
		  hz ? profiler_overhead_cycles() * 1000000000UL / hz : 0);
}

static void gen_adapt(struct pbuf *p)
{
	u8 conf;
	const char *why;
	enum task_class c = profiler_system_class(&conf, &why);
	pb_printf(p, "system_class: %s\nconfidence: %u\nreason: %s\npolicy: %s\n",
		  task_class_name(c), conf, why, sched_policy_name());
	static struct adapt_event ev[64];
	u32 total;
	u32 n = adapt_log_read(ev, 64, &total);
	pb_printf(p, "transitions: %u\n", total);
	for (u32 i = 0; i < n; i++)
		pb_printf(p, "%lu.%03lu tid=%d %s %s->%s conf=%u%% %s\n", ev[i].time_ns / 1000000000UL,
			  (ev[i].time_ns / 1000000) % 1000, ev[i].tid, ev[i].task,
			  task_class_name(ev[i].from), task_class_name(ev[i].to), ev[i].confidence,
			  ev[i].reason ? ev[i].reason : "");
}

static void gen_kmsg(struct pbuf *p)
{
	u64 sz = klog_size();
	u64 start = sz > p->cap ? sz - p->cap : 0;
	p->len = klog_read(p->b, p->cap, start);
}

static void gen_mounts(struct pbuf *p)
{
	u32 n;
	struct mount *m = vfs_mounts(&n);
	for (u32 i = 0; i < n; i++) {
		u64 total = 0, freeb = 0;
		u32 bs = 0;
		if (m[i].fs->statfs)
			m[i].fs->statfs(m[i].fsdata, &total, &freeb, &bs);
		pb_printf(p, "%-10s %-8s blocks=%lu free=%lu bsize=%u\n", m[i].path, m[i].fs->name,
			  total, freeb, bs);
	}
}

static void gen_interrupts(struct pbuf *p)
{
	for (int v = 0; v < 256; v++)
		if (irq_count((u8)v))
			pb_printf(p, "%3d: %lu\n", v, irq_count((u8)v));
}

static void gen_version(struct pbuf *p)
{
	pb_printf(p, "FuhrerOS %s (%s) x86_64 — own kernel, booted by %s\n", FUHREROS_VERSION,
		  FUHREROS_CODENAME, boot.bootloader);
}

static void gen_cmdline(struct pbuf *p) { pb_printf(p, "%s\n", boot.cmdline); }

/* Other subsystems add entries (net, bcache, pci, blk) through this table. */
typedef void (*proc_gen_t)(struct pbuf *p);
struct proc_entry {
	const char *name;
	proc_gen_t gen;
};
#define MAX_PROC_ENTRIES 32
static struct proc_entry entries[MAX_PROC_ENTRIES] = {
	{ "uptime", gen_uptime },   { "meminfo", gen_meminfo },	    { "cpuinfo", gen_cpuinfo },
	{ "tasks", gen_tasks },	    { "sched", gen_sched },	    { "adapt", gen_adapt },
	{ "kmsg", gen_kmsg },	    { "mounts", gen_mounts },	    { "interrupts", gen_interrupts },
	{ "version", gen_version }, { "cmdline", gen_cmdline },
};
static u32 nentries = 11;

void procfs_register(const char *name, proc_gen_t gen)
{
	if (nentries < MAX_PROC_ENTRIES)
		entries[nentries++] = (struct proc_entry){ name, gen };
}

static struct vnode proc_root;
static struct vnode proc_nodes[MAX_PROC_ENTRIES];
static const struct vnode_ops proc_file_ops;

static int p_lookup(struct vnode *dir, const char *name, struct vnode **out)
{
	for (u32 i = 0; i < nentries; i++)
		if (!strcmp(entries[i].name, name)) {
			struct vnode *vn = &proc_nodes[i];
			vn->type = VT_FILE;
			vn->ino = 1000 + i;
			vn->ops = &proc_file_ops;
			vn->priv = &entries[i];
			vn->refcnt = 1 << 20;
			vn->mnt = dir->mnt;
			vn->size = 0;
			*out = vn;
			return 0;
		}
	return -E_NOENT;
}

static int p_readdir(struct vnode *dir, u64 index, struct dirent *out)
{
	if (index >= nentries)
		return 0;
	out->ino = 1000 + (u32)index;
	out->type = VT_FILE;
	strlcpy(out->name, entries[index].name, sizeof(out->name));
	return 1;
}

static ssize_t p_read(struct vnode *vn, void *buf, u64 len, u64 off, int flags)
{
	struct proc_entry *e = vn->priv;
	struct pbuf p = { kmalloc(65536), 0, 65536 };
	if (!p.b)
		return -E_NOMEM;
	e->gen(&p);
	ssize_t n = 0;
	if (off < p.len) {
		n = (ssize_t)MIN(len, p.len - off);
		memcpy(buf, p.b + off, (u64)n);
	}
	kfree(p.b);
	return n;
}

static const struct vnode_ops proc_file_ops = { .read = p_read };
static const struct vnode_ops proc_dir_ops = { .lookup = p_lookup, .readdir = p_readdir };

static int procfs_mount(const char *src, struct vnode **root, void **fsdata)
{
	proc_root.type = VT_DIR;
	proc_root.ino = 1;
	proc_root.ops = &proc_dir_ops;
	proc_root.refcnt = 1 << 20;
	*root = &proc_root;
	*fsdata = NULL;
	return 0;
}
const struct fs_type procfs_type = { .name = "procfs", .mount = procfs_mount };
