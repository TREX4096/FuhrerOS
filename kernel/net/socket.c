/* Socket layer: sockets are file descriptors (VT_SOCK vnodes), so read(),
 * write(), close() and poll() work on them. Protocol processing runs in
 * netd; both sides serialise by disabling interrupts (uniprocessor). */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"
#include "net.h"
#include "netint.h"
#include "proc.h"
#include "vfs.h"

struct list_node all_sockets = LIST_INIT(all_sockets);
static u16 next_port = 49152;
#define DGQ 32

u16 ephemeral_port(void)
{
	for (;;) {
		u16 p = next_port++;
		if (next_port < 49152)
			next_port = 49152;
		bool used = false;
		list_for_each(it, &all_sockets) if (container_of(it, struct socket, node)->lport == p) used = true;
		if (!used)
			return p;
	}
}

struct socket *sock_alloc(int type)
{
	struct socket *s = kzalloc(sizeof(*s));
	if (!s)
		return NULL;
	s->type = type;
	s->refs = 1;
	wq_init(&s->rwait);
	wq_init(&s->wwait);
	wq_init(&s->await);
	list_init(&s->accept_q);
	list_init(&s->accept_node);
	if (type == SOCK_STREAM) {
		s->rx = vmalloc_pages(SOCK_RXBUF / PAGE_SIZE);
		s->tx = vmalloc_pages(SOCK_TXBUF / PAGE_SIZE);
	} else {
		s->dq = vmalloc_pages(ALIGN_UP(sizeof(struct dgram) * DGQ, PAGE_SIZE) / PAGE_SIZE);
	}
	struct netif *n = net_default_if();
	s->laddr = n ? n->ip : 0;
	u64 f = irq_save();
	list_push_back(&all_sockets, &s->node);
	irq_restore(f);
	return s;
}

static void sock_free(struct socket *s)
{
	list_remove(&s->node);
	if (s->rx)
		vfree_pages(s->rx, SOCK_RXBUF / PAGE_SIZE);
	if (s->tx)
		vfree_pages(s->tx, SOCK_TXBUF / PAGE_SIZE);
	if (s->dq)
		vfree_pages(s->dq, ALIGN_UP(sizeof(struct dgram) * DGQ, PAGE_SIZE) / PAGE_SIZE);
	kfree(s);
}

/* Closed stream sockets linger until TCP finishes; a reaper in netd's timer
 * path is not needed: we free them lazily here. */
static void reap_closed(void)
{
	list_for_each_safe(it, tmp, &all_sockets) {
		struct socket *s = container_of(it, struct socket, node);
		if (s->closed_by_user && s->type == SOCK_STREAM && s->state == TCP_CLOSED)
			sock_free(s);
	}
}

void sock_put(struct socket *s)
{
	u64 f = irq_save();
	if (--s->refs == 0) {
		s->closed_by_user = true;
		if (s->type == SOCK_STREAM && s->state != TCP_CLOSED && s->state != TCP_LISTEN) {
			tcp_close(s); /* freed once the FIN exchange completes */
		} else {
			if (s->type == SOCK_STREAM && s->state == TCP_LISTEN) {
				s->state = TCP_CLOSED;
				/* close never-accepted connections */
				while (!list_empty(&s->accept_q)) {
					struct socket *c = container_of(list_pop_front(&s->accept_q),
									struct socket, accept_node);
					c->closed_by_user = true;
					tcp_close(c);
				}
			}
			if (s->type != SOCK_STREAM)
				sock_free(s);
		}
	}
	reap_closed();
	irq_restore(f);
}

void sock_udp_input(u32 src, u16 sport, u16 dport, const u8 *data, u32 len)
{
	list_for_each(it, &all_sockets) {
		struct socket *s = container_of(it, struct socket, node);
		if (s->type != SOCK_DGRAM || s->lport != dport)
			continue;
		if (s->dq_head - s->dq_tail >= DGQ)
			return;
		struct dgram *d = &s->dq[s->dq_head % DGQ];
		d->addr = src;
		d->port = sport;
		d->len = (u16)MIN(len, sizeof(d->data));
		memcpy(d->data, data, d->len);
		s->dq_head++;
		wq_wake_all(&s->rwait);
		return;
	}
}

void sock_icmp_input(u32 src, const u8 *pkt, u32 len)
{
	u16 id = (u16)(pkt[4] << 8 | pkt[5]);
	list_for_each(it, &all_sockets) {
		struct socket *s = container_of(it, struct socket, node);
		if (s->type != SOCK_ICMP || s->lport != id || s->dq_head - s->dq_tail >= DGQ)
			continue;
		struct dgram *d = &s->dq[s->dq_head % DGQ];
		d->addr = src;
		d->port = 0;
		d->len = (u16)MIN(len, sizeof(d->data));
		memcpy(d->data, pkt, d->len);
		s->dq_head++;
		wq_wake_all(&s->rwait);
	}
}

static bool killed(void)
{
	struct process *p = proc_current();
	return p && p->killed;
}

/* ---- stream I/O ---- */
static ssize_t stream_recv(struct socket *s, u8 *buf, u64 len, bool nonblock)
{
	u64 f = irq_save();
	while (s->rx_head == s->rx_tail) {
		if (s->fin_received || s->state == TCP_CLOSED) {
			irq_restore(f);
			return s->error && !s->fin_received ? s->error : 0;
		}
		if (nonblock) {
			irq_restore(f);
			return -E_AGAIN;
		}
		wq_wait(&s->rwait, 0);
		if (killed()) {
			irq_restore(f);
			return -E_INTR;
		}
	}
	u64 n = 0;
	bool was_full = SOCK_RXBUF - (s->rx_head - s->rx_tail) < TCP_MSS;
	while (n < len && s->rx_tail != s->rx_head)
		buf[n++] = s->rx[s->rx_tail++ % SOCK_RXBUF];
	if (was_full && s->state != TCP_CLOSED) {
		/* window update so a stalled sender resumes */
		extern void tcp_output(struct socket *);
		tcp_output(s);
		u8 dummy = 0;
		(void)dummy;
	}
	irq_restore(f);
	return (ssize_t)n;
}

static ssize_t stream_send(struct socket *s, const u8 *buf, u64 len)
{
	u64 sent = 0;
	u64 f = irq_save();
	while (sent < len) {
		if (s->state != TCP_ESTABLISHED && s->state != TCP_CLOSE_WAIT) {
			irq_restore(f);
			return sent ? (ssize_t)sent : (s->error ? s->error : -E_NOTCONN);
		}
		u32 space = SOCK_TXBUF - (s->tx_head - s->tx_tail);
		if (!space) {
			wq_wait(&s->wwait, 0);
			if (killed()) {
				irq_restore(f);
				return -E_INTR;
			}
			continue;
		}
		u32 n = (u32)MIN(space, len - sent);
		for (u32 i = 0; i < n; i++)
			s->tx[(s->tx_head + i) % SOCK_TXBUF] = buf[sent + i];
		s->tx_head += n;
		sent += n;
		tcp_output(s);
	}
	irq_restore(f);
	return (ssize_t)sent;
}

static ssize_t dgram_recv(struct socket *s, u8 *buf, u64 len, u32 *addr, u16 *port, bool nonblock,
			  u64 timeout_ms)
{
	u64 f = irq_save();
	u64 deadline = timeout_ms ? time_ns() + timeout_ms * 1000000ULL : 0;
	while (s->dq_head == s->dq_tail) {
		if (nonblock) {
			irq_restore(f);
			return -E_AGAIN;
		}
		u64 wait = 0;
		if (deadline) {
			u64 now = time_ns();
			if (now >= deadline) {
				irq_restore(f);
				return -E_TIMEDOUT;
			}
			wait = deadline - now;
		}
		wq_wait(&s->rwait, wait);
		if (killed()) {
			irq_restore(f);
			return -E_INTR;
		}
	}
	struct dgram *d = &s->dq[s->dq_tail++ % DGQ];
	u64 n = MIN(len, d->len);
	memcpy(buf, d->data, n);
	if (addr)
		*addr = d->addr;
	if (port)
		*port = d->port;
	irq_restore(f);
	return (ssize_t)n;
}

/* ---- vnode glue ---- */
static ssize_t sv_read(struct vnode *vn, void *buf, u64 len, u64 off, int flags)
{
	struct socket *s = vn->priv;
	if (s->type == SOCK_STREAM)
		return stream_recv(s, buf, len, flags & O_NONBLOCK);
	return dgram_recv(s, buf, len, NULL, NULL, flags & O_NONBLOCK, 0);
}

static ssize_t sv_write(struct vnode *vn, const void *buf, u64 len, u64 off, int flags)
{
	struct socket *s = vn->priv;
	if (s->type == SOCK_STREAM)
		return stream_send(s, buf, len);
	if (!s->raddr)
		return -E_NOTCONN;
	u64 f = irq_save();
	int r = udp_send(s->raddr, s->lport, s->rport, buf, (u32)len);
	irq_restore(f);
	return r < 0 ? r : (ssize_t)len;
}

static int sv_poll(struct vnode *vn, int events)
{
	struct socket *s = vn->priv;
	int r = 0;
	if (s->type == SOCK_STREAM) {
		if (s->rx_head != s->rx_tail || s->fin_received || s->state == TCP_CLOSED ||
		    !list_empty(&s->accept_q))
			r |= 1;
		if (SOCK_TXBUF - (s->tx_head - s->tx_tail))
			r |= 2;
	} else {
		if (s->dq_head != s->dq_tail)
			r |= 1;
		r |= 2;
	}
	return r;
}

static void sv_release(struct vnode *vn)
{
	sock_put(vn->priv);
	kfree(vn);
}

static const struct vnode_ops sock_ops = {
	.read = sv_read, .write = sv_write, .poll = sv_poll, .release = sv_release };

static int sock_fd(struct socket *s)
{
	struct vnode *vn = kzalloc(sizeof(*vn));
	vn->type = VT_SOCK;
	vn->ops = &sock_ops;
	vn->priv = s;
	vn->refcnt = 1;
	struct file *f = file_from_vnode(vn, O_RDWR);
	int fd = proc_fd_alloc(proc_current(), f);
	if (fd < 0)
		file_close(f);
	return fd;
}

static struct socket *fd_sock(int fd)
{
	struct file *f = proc_fd_get(proc_current(), fd);
	if (!f || f->vn->type != VT_SOCK)
		return NULL;
	return f->vn->priv;
}

void sock_init(void) { KLOG("net", "sockets: TCP, UDP, ICMP echo"); }

i64 sys_net(u64 nr, u64 a, u64 b, u64 c, u64 d, u64 e)
{
	struct netif *nif = net_default_if();
	if (nr == SYS_NETINFO) {
		struct fu_netinfo ni;
		net_fill_info(&ni);
		return copy_to_user((void *)a, &ni, sizeof(ni));
	}
	if (!nif)
		return -E_NETUNREACH;
	if (nr == SYS_SOCKET) {
		if (a != AF_INET || (b != SOCK_STREAM && b != SOCK_DGRAM && b != SOCK_ICMP))
			return -E_INVAL;
		struct socket *s = sock_alloc((int)b);
		if (!s)
			return -E_NOMEM;
		if (b == SOCK_ICMP)
			s->lport = (u16)(proc_current()->pid + 0x4600);
		int fd = sock_fd(s);
		if (fd < 0)
			sock_put(s);
		return fd;
	}
	struct socket *s = fd_sock((int)a);
	if (!s)
		return -E_NOTSOCK;
	struct sockaddr_in sa;
	switch (nr) {
	case SYS_CONNECT: {
		if (copy_from_user(&sa, (void *)b, sizeof(sa)) < 0)
			return -E_FAULT;
		u64 f = irq_save();
		s->raddr = ntohl(sa.addr);
		s->rport = ntohs(sa.port);
		s->laddr = nif->ip;
		if (!s->lport)
			s->lport = ephemeral_port();
		if (s->type != SOCK_STREAM) {
			irq_restore(f);
			return 0;
		}
		tcp_connect(s);
		while (s->state == TCP_SYN_SENT) {
			wq_wait(&s->await, 0);
			if (killed()) {
				irq_restore(f);
				return -E_INTR;
			}
		}
		int r = s->state == TCP_ESTABLISHED ? 0 : (s->error ? s->error : -E_CONNREFUSED);
		irq_restore(f);
		return r;
	}
	case SYS_BIND: {
		if (copy_from_user(&sa, (void *)b, sizeof(sa)) < 0)
			return -E_FAULT;
		u16 port = ntohs(sa.port);
		list_for_each(it, &all_sockets) {
			struct socket *o = container_of(it, struct socket, node);
			if (o != s && o->lport == port && o->type == s->type && o->state != TCP_CLOSED)
				return -E_ADDRINUSE;
		}
		s->lport = port ? port : ephemeral_port();
		return 0;
	}
	case SYS_LISTEN:
		if (s->type != SOCK_STREAM || !s->lport)
			return -E_INVAL;
		s->state = TCP_LISTEN;
		s->backlog = b ? (int)b : 8;
		return 0;
	case SYS_ACCEPT: {
		if (s->state != TCP_LISTEN)
			return -E_INVAL;
		u64 f = irq_save();
		while (list_empty(&s->accept_q)) {
			wq_wait(&s->await, 0);
			if (killed()) {
				irq_restore(f);
				return -E_INTR;
			}
		}
		struct socket *c = container_of(list_pop_front(&s->accept_q), struct socket, accept_node);
		irq_restore(f);
		if (b) {
			struct sockaddr_in ca = { AF_INET, htons(c->rport), htonl(c->raddr), { 0 } };
			copy_to_user((void *)b, &ca, sizeof(ca));
		}
		return sock_fd(c);
	}
	case SYS_SEND:
		if (!user_range_ok((void *)b, c, false))
			return -E_FAULT;
		sched_account_io();
		return s->type == SOCK_STREAM ? stream_send(s, (void *)b, c)
					      : sv_write(NULL, (void *)b, c, 0, 0);
	case SYS_RECV:
		if (!user_range_ok((void *)b, c, true))
			return -E_FAULT;
		sched_account_io();
		if (s->type == SOCK_STREAM)
			return stream_recv(s, (void *)b, c, false);
		return dgram_recv(s, (void *)b, c, NULL, NULL, false, d);
	case SYS_SENDTO: {
		if (copy_from_user(&sa, (void *)d, sizeof(sa)) < 0 || !user_range_ok((void *)b, c, false))
			return -E_FAULT;
		sched_account_io();
		u64 f = irq_save();
		int r;
		if (s->type == SOCK_ICMP) {
			r = ip_send(ntohl(sa.addr), 1, (void *)b, (u32)c);
		} else {
			if (!s->lport)
				s->lport = ephemeral_port();
			r = udp_send(ntohl(sa.addr), s->lport, ntohs(sa.port), (void *)b, (u32)c);
		}
		irq_restore(f);
		return r < 0 ? r : (i64)c;
	}
	case SYS_RECVFROM: {
		if (!user_range_ok((void *)b, c, true))
			return -E_FAULT;
		sched_account_io();
		u32 addr = 0;
		u16 port = 0;
		/* d = &sockaddr (may be 0), e = timeout ms */
		ssize_t n = dgram_recv(s, (void *)b, c, &addr, &port, false, e);
		if (n >= 0 && d) {
			struct sockaddr_in from = { AF_INET, htons(port), htonl(addr), { 0 } };
			copy_to_user((void *)d, &from, sizeof(from));
		}
		return n;
	}
	}
	return -E_NOSYS;
}
