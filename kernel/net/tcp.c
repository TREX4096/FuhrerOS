/* TCP (RFC 793 subset): three-way handshake (active and passive), sliding
 * window with cumulative ACKs, go-back-N retransmission with exponential
 * back-off, in-order reception (out-of-order segments are dropped and the
 * peer retransmits), FIN handshakes, RST handling. No congestion control,
 * SACK, or window scaling — documented limitations. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"
#include "net.h"
#include "netint.h"

#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

#define SEQ_LT(a, b) ((i32)((a) - (b)) < 0)
#define SEQ_LEQ(a, b) ((i32)((a) - (b)) <= 0)

static u32 rx_free(struct socket *s) { return SOCK_RXBUF - (s->rx_head - s->rx_tail); }

static void send_segment(struct socket *s, u32 seq, u8 flags, const u8 *data, u32 len)
{
	u8 seg[20 + TCP_MSS + 4];
	u32 hl = 20;
	u16 sp = htons(s->lport), dp = htons(s->rport);
	memcpy(seg, &sp, 2);
	memcpy(seg + 2, &dp, 2);
	u32 sq = htonl(seq), ak = htonl(s->rcv_nxt);
	memcpy(seg + 4, &sq, 4);
	memcpy(seg + 8, &ak, 4);
	if (flags & F_SYN) { /* MSS option */
		hl = 24;
		seg[20] = 2;
		seg[21] = 4;
		seg[22] = (u8)(TCP_MSS >> 8);
		seg[23] = (u8)TCP_MSS;
	}
	seg[12] = (u8)((hl / 4) << 4);
	seg[13] = flags;
	u32 w = rx_free(s);
	u16 win = htons((u16)(w > 65535 ? 65535 : w));
	memcpy(seg + 14, &win, 2);
	seg[16] = seg[17] = seg[18] = seg[19] = 0;
	if (len)
		memcpy(seg + hl, data, len);
	u16 cs = htons(net_checksum(seg, hl + len, pseudo_sum(s->laddr, s->raddr, 6, (u16)(hl + len))));
	memcpy(seg + 16, &cs, 2);
	ip_send(s->raddr, 6, seg, hl + len);
}

static void send_ack(struct socket *s) { send_segment(s, s->snd_nxt, F_ACK, NULL, 0); }

/* The application drained a (nearly) full receive buffer: advertise the
 * reopened window, otherwise a sender that saw a zero window waits forever. */
void tcp_window_update(struct socket *s)
{
	if (s->state == TCP_ESTABLISHED || s->state == TCP_FIN_WAIT1 || s->state == TCP_FIN_WAIT2)
		send_ack(s);
}

static void send_rst_reply(u32 src, u32 dst, const u8 *seg, u32 len)
{
	/* RST for a segment that matches no socket */
	struct socket tmp;
	memset(&tmp, 0, sizeof(tmp));
	tmp.laddr = dst;
	tmp.raddr = src;
	tmp.lport = (u16)(seg[2] << 8 | seg[3]);
	tmp.rport = (u16)(seg[0] << 8 | seg[1]);
	u32 seq = (u32)(seg[4] << 24 | seg[5] << 16 | seg[6] << 8 | seg[7]);
	u32 ack = (u32)(seg[8] << 24 | seg[9] << 16 | seg[10] << 8 | seg[11]);
	u8 flags = seg[13];
	u32 hl = (u32)(seg[12] >> 4) * 4;
	u32 dlen = len - hl + ((flags & F_SYN) ? 1 : 0) + ((flags & F_FIN) ? 1 : 0);
	if (flags & F_ACK) {
		tmp.rcv_nxt = 0;
		send_segment(&tmp, ack, F_RST, NULL, 0);
	} else {
		tmp.rcv_nxt = seq + dlen;
		send_segment(&tmp, 0, F_RST | F_ACK, NULL, 0);
	}
}

static void set_rto(struct socket *s)
{
	if (!s->rto_ns)
		s->rto_ns = 300000000ULL; /* 300 ms initial */
	s->rto_deadline = time_ns() + s->rto_ns;
}

/* Send whatever the window allows: unsent data, then a pending FIN. */
void tcp_output(struct socket *s)
{
	if (s->state != TCP_ESTABLISHED && s->state != TCP_CLOSE_WAIT &&
	    s->state != TCP_FIN_WAIT1 && s->state != TCP_LAST_ACK && s->state != TCP_CLOSING)
		return;
	u32 wnd = s->snd_wnd; /* zero window: send nothing; the persist timer probes */
	for (;;) {
		u32 sent_unacked = s->snd_nxt - s->snd_una;
		u32 queued = s->tx_head - s->tx_tail; /* bytes not yet acked */
		if (s->fin_sent && sent_unacked)
			sent_unacked--; /* FIN occupies one sequence number */
		if (sent_unacked >= queued || sent_unacked >= wnd)
			break;
		u32 n = MIN(queued - sent_unacked, wnd - sent_unacked);
		n = MIN(n, TCP_MSS);
		u8 buf[TCP_MSS];
		for (u32 i = 0; i < n; i++)
			buf[i] = s->tx[(s->tx_tail + sent_unacked + i) % SOCK_TXBUF];
		send_segment(s, s->snd_nxt, F_ACK | F_PSH, buf, n);
		s->snd_nxt += n;
		if (!s->rto_deadline)
			set_rto(s);
	}
	if (s->fin_pending && !s->fin_sent && s->tx_head == s->tx_tail + (s->snd_nxt - s->snd_una)) {
		send_segment(s, s->snd_nxt, F_FIN | F_ACK, NULL, 0);
		s->snd_nxt++;
		s->fin_sent = true;
		if (s->state == TCP_ESTABLISHED)
			s->state = TCP_FIN_WAIT1;
		else if (s->state == TCP_CLOSE_WAIT)
			s->state = TCP_LAST_ACK;
		set_rto(s);
	}
}

int tcp_connect(struct socket *s)
{
	s->iss = (u32)rdtsc();
	s->snd_una = s->iss;
	s->snd_nxt = s->iss + 1;
	s->state = TCP_SYN_SENT;
	s->rto_ns = 0;
	send_segment(s, s->iss, F_SYN, NULL, 0);
	set_rto(s);
	return 0;
}

void tcp_close(struct socket *s)
{
	switch (s->state) {
	case TCP_ESTABLISHED:
	case TCP_CLOSE_WAIT:
		s->fin_pending = true;
		tcp_output(s);
		break;
	case TCP_SYN_SENT:
	case TCP_LISTEN:
	case TCP_SYN_RCVD:
		s->state = TCP_CLOSED;
		break;
	default:
		break;
	}
}

static void wake(struct socket *s)
{
	wq_wake_all(&s->rwait);
	wq_wake_all(&s->wwait);
	wq_wake_all(&s->await);
}

static void become_closed(struct socket *s, int err)
{
	s->state = TCP_CLOSED;
	if (err)
		s->error = err;
	s->rto_deadline = 0;
	wake(s);
	if (s->parent) { /* embryonic connection failed */
		s->parent->pending--;
		s->parent = NULL;
	}
}

static struct socket *find(u32 src, u16 sport, u16 dport, bool *listener)
{
	struct socket *l = NULL;
	list_for_each(it, &all_sockets) {
		struct socket *s = container_of(it, struct socket, node);
		if (s->type != SOCK_STREAM || s->lport != dport)
			continue;
		if (s->state == TCP_LISTEN) {
			l = s;
			continue;
		}
		if (s->raddr == src && s->rport == sport && s->state != TCP_CLOSED)
			return s;
	}
	*listener = l != NULL;
	return l;
}

void tcp_input(u32 src, u32 dst, const u8 *seg, u32 len)
{
	if (len < 20 || net_checksum(seg, len, pseudo_sum(src, dst, 6, (u16)len)) != 0)
		return;
	u16 sport = (u16)(seg[0] << 8 | seg[1]), dport = (u16)(seg[2] << 8 | seg[3]);
	u32 seq = (u32)(seg[4] << 24 | seg[5] << 16 | seg[6] << 8 | seg[7]);
	u32 ack = (u32)(seg[8] << 24 | seg[9] << 16 | seg[10] << 8 | seg[11]);
	u32 hl = (u32)(seg[12] >> 4) * 4;
	u8 flags = seg[13];
	u32 win = (u32)(seg[14] << 8 | seg[15]);
	if (hl < 20 || hl > len)
		return;
	const u8 *data = seg + hl;
	u32 dlen = len - hl;

	bool is_listener = false;
	struct socket *s = find(src, sport, dport, &is_listener);
	if (!s) {
		if (!(flags & F_RST))
			send_rst_reply(src, dst, seg, len);
		return;
	}
	if (is_listener) {
		if (!(flags & F_SYN) || (flags & F_ACK))
			return;
		if (s->pending >= s->backlog)
			return; /* drop: client retries */
		struct socket *c = sock_alloc(SOCK_STREAM);
		if (!c)
			return;
		c->laddr = dst;
		c->lport = dport;
		c->raddr = src;
		c->rport = sport;
		c->irs = seq;
		c->rcv_nxt = seq + 1;
		c->iss = (u32)rdtsc();
		c->snd_una = c->iss;
		c->snd_nxt = c->iss + 1;
		c->snd_wnd = win;
		c->state = TCP_SYN_RCVD;
		c->parent = s;
		s->pending++;
		send_segment(c, c->iss, F_SYN | F_ACK, NULL, 0);
		set_rto(c);
		return;
	}
	if (flags & F_RST) {
		become_closed(s, -E_CONNRESET);
		return;
	}
	switch (s->state) {
	case TCP_SYN_SENT:
		if ((flags & (F_SYN | F_ACK)) == (F_SYN | F_ACK) && ack == s->iss + 1) {
			s->irs = seq;
			s->rcv_nxt = seq + 1;
			s->snd_una = ack;
			s->snd_wnd = win;
			s->state = TCP_ESTABLISHED;
			s->rto_deadline = 0;
			s->retries = 0;
			send_ack(s);
			wake(s);
		}
		return;
	case TCP_SYN_RCVD:
		if ((flags & F_ACK) && ack == s->iss + 1) {
			s->snd_una = ack;
			s->snd_wnd = win;
			s->state = TCP_ESTABLISHED;
			s->rto_deadline = 0;
			if (s->parent) {
				struct socket *l = s->parent;
				l->pending--;
				s->parent = NULL;
				list_push_back(&l->accept_q, &s->accept_node);
				wq_wake_all(&l->await);
			}
		} else {
			return;
		}
		break;
	default:
		break;
	}

	/* duplicate ACKs: three in a row mean a segment was lost -> fast
	 * retransmit instead of waiting for the timeout */
	if ((flags & F_ACK) && ack == s->snd_una && s->snd_nxt != s->snd_una && !dlen &&
	    win == s->snd_wnd && !(flags & (F_SYN | F_FIN))) {
		if (++s->dupacks == 3) {
			s->retransmits++;
			s->snd_nxt = s->snd_una;
			if (s->fin_sent) {
				s->fin_sent = false;
				if (s->state == TCP_FIN_WAIT1)
					s->state = TCP_ESTABLISHED;
				else if (s->state == TCP_LAST_ACK)
					s->state = TCP_CLOSE_WAIT;
			}
		}
	} else if (flags & F_ACK) {
		s->dupacks = 0;
	}

	/* ACK processing */
	if ((flags & F_ACK) && SEQ_LT(s->snd_una, ack) && SEQ_LEQ(ack, s->snd_nxt)) {
		u32 acked = ack - s->snd_una;
		if (s->fin_sent && ack == s->snd_nxt)
			acked--; /* FIN's sequence number carries no data */
		s->tx_tail += MIN(acked, s->tx_head - s->tx_tail);
		s->snd_una = ack;
		s->retries = 0;
		s->rto_ns = 0;
		s->rto_deadline = s->snd_una == s->snd_nxt ? 0 : time_ns() + 300000000ULL;
		/* persist timer: data waiting behind a zero window gets probed */
		if (!s->rto_deadline && win == 0 && s->tx_head != s->tx_tail)
			s->rto_deadline = time_ns() + 200000000ULL;
		wq_wake_all(&s->wwait);
		if (s->fin_sent && ack == s->snd_nxt) {
			if (s->state == TCP_FIN_WAIT1)
				s->state = TCP_FIN_WAIT2;
			else if (s->state == TCP_CLOSING) {
				s->state = TCP_TIME_WAIT;
				s->timewait_until = time_ns() + 1000000000ULL;
			} else if (s->state == TCP_LAST_ACK) {
				become_closed(s, 0);
				return;
			}
		}
	}
	if (flags & F_ACK)
		s->snd_wnd = win;

	/* data: accept only the next in-order bytes that fit */
	if (dlen) {
		if (seq == s->rcv_nxt && !s->fin_received) {
			u32 n = MIN(dlen, rx_free(s));
			for (u32 i = 0; i < n; i++)
				s->rx[(s->rx_head + i) % SOCK_RXBUF] = data[i];
			s->rx_head += n;
			s->rcv_nxt += n;
			wq_wake_all(&s->rwait);
		}
		send_ack(s); /* also a duplicate ACK for out-of-order data */
	}
	if ((flags & F_FIN) && seq + dlen == s->rcv_nxt && !s->fin_received) {
		s->rcv_nxt++;
		s->fin_received = true;
		send_ack(s);
		if (s->state == TCP_ESTABLISHED)
			s->state = TCP_CLOSE_WAIT;
		else if (s->state == TCP_FIN_WAIT1)
			s->state = TCP_CLOSING;
		else if (s->state == TCP_FIN_WAIT2) {
			s->state = TCP_TIME_WAIT;
			s->timewait_until = time_ns() + 1000000000ULL;
		}
		wake(s);
	}
	tcp_output(s);
}

/* Called every ~10 ms from netd. */
void tcp_timer(void)
{
	u64 now = time_ns();
	list_for_each_safe(it, tmp, &all_sockets) {
		struct socket *s = container_of(it, struct socket, node);
		if (s->type != SOCK_STREAM)
			continue;
		if (s->state == TCP_TIME_WAIT && now >= s->timewait_until) {
			become_closed(s, 0);
			continue;
		}
		if (!s->rto_deadline || now < s->rto_deadline)
			continue;
		if (++s->retries > 8) {
			become_closed(s, -E_TIMEDOUT);
			continue;
		}
		s->rto_ns = s->rto_ns ? MIN(s->rto_ns * 2, 8000000000ULL) : 300000000ULL;
		s->retransmits++;
		if ((s->state == TCP_ESTABLISHED || s->state == TCP_CLOSE_WAIT) && s->snd_wnd == 0 &&
		    s->snd_nxt == s->snd_una && s->tx_head != s->tx_tail) {
			/* zero-window probe: one byte of new data forces an ACK
			 * that carries the receiver's current window */
			u8 b = s->tx[s->tx_tail % SOCK_TXBUF];
			send_segment(s, s->snd_nxt, F_ACK | F_PSH, &b, 1);
			s->snd_nxt++;
			s->retries = 0; /* probing is not a failure */
		} else if (s->state == TCP_SYN_SENT) {
			send_segment(s, s->iss, F_SYN, NULL, 0);
		} else if (s->state == TCP_SYN_RCVD) {
			send_segment(s, s->iss, F_SYN | F_ACK, NULL, 0);
		} else {
			/* go-back-N: resend from the first unacknowledged byte */
			s->snd_nxt = s->snd_una;
			if (s->fin_sent) {
				s->fin_sent = false;
				if (s->state == TCP_FIN_WAIT1)
					s->state = TCP_ESTABLISHED;
				else if (s->state == TCP_LAST_ACK)
					s->state = TCP_CLOSE_WAIT;
			}
			tcp_output(s);
		}
		s->rto_deadline = now + s->rto_ns;
	}
}

static const char *state_names[] = { "CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD", "ESTABLISHED",
				      "FIN_WAIT1", "FIN_WAIT2", "CLOSE_WAIT", "CLOSING",
				      "LAST_ACK", "TIME_WAIT" };

struct pbuf;
void pb_printf(struct pbuf *p, const char *fmt, ...);
void tcp_dump(void *pb)
{
	list_for_each(it, &all_sockets) {
		struct socket *s = container_of(it, struct socket, node);
		const char *t = s->type == SOCK_STREAM ? "tcp" : s->type == SOCK_DGRAM ? "udp" : "icmp";
		pb_printf(pb, "%s %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u %s rx=%u tx=%u retrans=%lu\n", t,
			  s->laddr >> 24, (s->laddr >> 16) & 255, (s->laddr >> 8) & 255, s->laddr & 255,
			  s->lport, s->raddr >> 24, (s->raddr >> 16) & 255, (s->raddr >> 8) & 255,
			  s->raddr & 255, s->rport, s->type == SOCK_STREAM ? state_names[s->state] : "-",
			  s->rx_head - s->rx_tail, s->tx_head - s->tx_tail, s->retransmits);
	}
}

void tcp_counts(u32 *tcp, u32 *udp)
{
	*tcp = *udp = 0;
	list_for_each(it, &all_sockets) {
		struct socket *s = container_of(it, struct socket, node);
		if (s->type == SOCK_STREAM && s->state != TCP_CLOSED)
			(*tcp)++;
		else if (s->type == SOCK_DGRAM)
			(*udp)++;
	}
}
