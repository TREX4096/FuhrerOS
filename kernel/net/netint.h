/* Network stack internals shared between stack.c, tcp.c and socket.c. */
#ifndef FUHRER_NETINT_H
#define FUHRER_NETINT_H
#include "list.h"
#include "sched.h"
#include "types.h"
#include "uapi/fuhrer.h"

#define SOCK_RXBUF 65536
#define SOCK_TXBUF 65536
#define TCP_MSS 1400

enum tcp_state {
	TCP_CLOSED, TCP_LISTEN, TCP_SYN_SENT, TCP_SYN_RCVD, TCP_ESTABLISHED, TCP_FIN_WAIT1,
	TCP_FIN_WAIT2, TCP_CLOSE_WAIT, TCP_CLOSING, TCP_LAST_ACK, TCP_TIME_WAIT
};

struct dgram {
	u32 addr;
	u16 port;
	u16 len;
	u8 data[1472];
};

struct socket {
	int type;			/* SOCK_STREAM / SOCK_DGRAM / SOCK_ICMP */
	u32 laddr, raddr;		/* host byte order */
	u16 lport, rport;
	int refs;			/* open files + protocol references */
	bool closed_by_user;
	struct waitqueue rwait, wwait, await;
	struct list_node node;
	/* stream buffers */
	u8 *rx;
	u32 rx_head, rx_tail;		/* bytes: head - tail available */
	u8 *tx;
	u32 tx_head, tx_tail;		/* tx_tail = first unacknowledged byte */
	/* datagrams */
	struct dgram *dq;
	u32 dq_head, dq_tail;
	/* TCP */
	enum tcp_state state;
	u32 iss, snd_una, snd_nxt, snd_wnd;
	u32 irs, rcv_nxt;
	bool fin_pending, fin_sent, fin_received;
	u64 rto_ns, rto_deadline, timewait_until;
	u32 retries;
	u32 dupacks;
	int error;
	struct socket *parent;		/* listening socket of an embryonic conn. */
	struct list_node accept_q;	/* completed connections (listener) */
	struct list_node accept_node;
	int backlog, pending;
	u64 retransmits;
};

extern struct list_node all_sockets;

void sock_init(void);
void sock_udp_input(u32 src, u16 sport, u16 dport, const u8 *data, u32 len);
void sock_icmp_input(u32 src, const u8 *pkt, u32 len);
void tcp_input(u32 src, u32 dst, const u8 *seg, u32 len);
void tcp_timer(void);
void tcp_output(struct socket *s);
int tcp_connect(struct socket *s);
void tcp_close(struct socket *s);
void tcp_dump(void *pbuf);
void tcp_counts(u32 *tcp, u32 *udp);
struct socket *sock_alloc(int type);
void sock_put(struct socket *s);
u16 net_checksum(const void *data, u32 len, u32 initial);
u32 pseudo_sum(u32 src, u32 dst, u8 proto, u16 len);
u16 ephemeral_port(void);
void net_fill_info(struct fu_netinfo *ni);
#endif
