/* Inter-process communication (NEW_EXPLANATION §1 item 12):
 *   pipes         byte streams between processes (shell pipelines, the
 *                 desktop terminal <-> shell link)
 *   message ports named mailboxes of fixed-size messages (up to 256 B),
 *                 used by desktop services; receivers block, senders never
 *                 block (queue full -> -E_AGAIN). */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"
#include "proc.h"
#include "sched.h"
#include "vfs.h"

#define PIPE_SIZE 16384

struct pipe {
	u8 buf[PIPE_SIZE];
	u64 head, tail;
	int readers, writers;
	struct waitqueue rwait, wwait;
};

static bool proc_killed(void)
{
	struct process *p = proc_current();
	return p && p->killed;
}

static ssize_t pipe_read(struct vnode *vn, void *buf, u64 len, u64 off, int flags)
{
	struct pipe *p = vn->priv;
	u64 f = irq_save();
	while (p->head == p->tail) {
		if (p->writers == 0) {
			irq_restore(f);
			return 0; /* EOF */
		}
		if (flags & O_NONBLOCK) {
			irq_restore(f);
			return -E_AGAIN;
		}
		wq_wait(&p->rwait, 0);
		if (proc_killed()) {
			irq_restore(f);
			return -E_INTR;
		}
	}
	u64 n = 0;
	u8 *b = buf;
	while (n < len && p->tail != p->head)
		b[n++] = p->buf[p->tail++ % PIPE_SIZE];
	wq_wake_all(&p->wwait);
	irq_restore(f);
	return (ssize_t)n;
}

static ssize_t pipe_write(struct vnode *vn, const void *buf, u64 len, u64 off, int flags)
{
	struct pipe *p = vn->priv;
	const u8 *b = buf;
	u64 n = 0;
	u64 f = irq_save();
	while (n < len) {
		if (p->readers == 0) {
			irq_restore(f);
			return n ? (ssize_t)n : -E_PIPE;
		}
		if (p->head - p->tail == PIPE_SIZE) {
			wq_wake_all(&p->rwait);
			if (flags & O_NONBLOCK) {
				irq_restore(f);
				return n ? (ssize_t)n : -E_AGAIN;
			}
			wq_wait(&p->wwait, 0);
			if (proc_killed()) {
				irq_restore(f);
				return -E_INTR;
			}
			continue;
		}
		p->buf[p->head++ % PIPE_SIZE] = b[n++];
	}
	wq_wake_all(&p->rwait);
	irq_restore(f);
	return (ssize_t)n;
}

static void pipe_close(struct vnode *vn, int flags)
{
	struct pipe *p = vn->priv;
	u64 f = irq_save();
	if ((flags & O_ACCMODE) == O_RDONLY)
		p->readers--;
	else
		p->writers--;
	wq_wake_all(&p->rwait);
	wq_wake_all(&p->wwait);
	irq_restore(f);
}

static void pipe_release(struct vnode *vn)
{
	kfree(vn->priv);
	kfree(vn);
}

static int pipe_poll(struct vnode *vn, int events)
{
	struct pipe *p = vn->priv;
	int r = 0;
	if (p->head != p->tail || p->writers == 0)
		r |= 1;
	if (p->head - p->tail < PIPE_SIZE)
		r |= 2;
	return r;
}

static const struct vnode_ops pipe_ops = {
	.read = pipe_read, .write = pipe_write, .close = pipe_close, .release = pipe_release,
	.poll = pipe_poll,
};

int pipe_create(struct file **rd, struct file **wr)
{
	struct pipe *p = kzalloc(sizeof(*p));
	struct vnode *vn = kzalloc(sizeof(*vn));
	if (!p || !vn) {
		kfree(p);
		kfree(vn);
		return -E_NOMEM;
	}
	wq_init(&p->rwait);
	wq_init(&p->wwait);
	p->readers = p->writers = 1;
	vn->type = VT_PIPE;
	vn->ops = &pipe_ops;
	vn->priv = p;
	vn->refcnt = 2; /* one per file */
	*rd = file_from_vnode(vn, O_RDONLY);
	*wr = file_from_vnode(vn, O_WRONLY);
	return 0;
}

/* ---- message ports ---- */
#define MAX_PORTS 32
#define PORT_QUEUE 32
#define MSG_MAX 256

struct msg {
	pid_t sender;
	u32 len;
	u8 data[MSG_MAX];
};

struct port {
	char name[32];
	pid_t owner;
	struct msg q[PORT_QUEUE];
	u32 head, tail;
	struct waitqueue wait;
	bool used;
};

static struct port ports[MAX_PORTS];

int port_create(const char *name)
{
	u64 f = irq_save();
	for (int i = 0; i < MAX_PORTS; i++)
		if (ports[i].used && !strcmp(ports[i].name, name)) {
			irq_restore(f);
			return -E_EXIST;
		}
	for (int i = 0; i < MAX_PORTS; i++) {
		if (!ports[i].used) {
			memset(&ports[i], 0, sizeof(ports[i]));
			ports[i].used = true;
			strlcpy(ports[i].name, name, sizeof(ports[i].name));
			struct process *p = proc_current();
			ports[i].owner = p ? p->pid : 0;
			wq_init(&ports[i].wait);
			irq_restore(f);
			return i;
		}
	}
	irq_restore(f);
	return -E_NOSPC;
}

int port_lookup(const char *name)
{
	for (int i = 0; i < MAX_PORTS; i++)
		if (ports[i].used && !strcmp(ports[i].name, name))
			return i;
	return -E_NOENT;
}

int port_send(int id, const void *data, u32 len)
{
	if (id < 0 || id >= MAX_PORTS || !ports[id].used || len > MSG_MAX)
		return -E_INVAL;
	struct port *p = &ports[id];
	u64 f = irq_save();
	if (p->head - p->tail == PORT_QUEUE) {
		irq_restore(f);
		return -E_AGAIN;
	}
	struct msg *m = &p->q[p->head++ % PORT_QUEUE];
	struct process *me = proc_current();
	m->sender = me ? me->pid : 0;
	m->len = len;
	memcpy(m->data, data, len);
	wq_wake_one(&p->wait);
	irq_restore(f);
	return 0;
}

/* Blocking receive; timeout_ms 0 = forever. Returns length or -errno. */
int port_recv(int id, void *data, u32 max, pid_t *sender, u64 timeout_ms)
{
	if (id < 0 || id >= MAX_PORTS || !ports[id].used)
		return -E_INVAL;
	struct port *p = &ports[id];
	u64 f = irq_save();
	while (p->head == p->tail) {
		int r = wq_wait(&p->wait, timeout_ms * 1000000ULL);
		if (r == -E_TIMEDOUT || proc_killed()) {
			irq_restore(f);
			return r == -E_TIMEDOUT ? r : -E_INTR;
		}
	}
	struct msg *m = &p->q[p->tail++ % PORT_QUEUE];
	u32 n = MIN(m->len, max);
	memcpy(data, m->data, n);
	if (sender)
		*sender = m->sender;
	irq_restore(f);
	return (int)n;
}

void port_destroy_owned(pid_t pid)
{
	for (int i = 0; i < MAX_PORTS; i++)
		if (ports[i].used && ports[i].owner == pid) {
			ports[i].used = false;
			wq_wake_all(&ports[i].wait);
		}
}
