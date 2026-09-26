/* Ethernet, ARP, IPv4, ICMP, UDP, DHCP, the receive thread, and the socket
 * layer (TCP lives in net/tcp.c). Received frames are copied into a ring in
 * interrupt context and processed by the `netd` kernel thread, which also
 * drives protocol timers. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"
#include "net.h"
#include "netint.h"
#include "proc.h"
#include "sched.h"
#include "vfs.h"

static struct netif *ifs;
static struct waitqueue netd_wq;

/* ---- receive ring ---- */
#define RXQ 256
static struct {
	u8 data[PKT_MAX];
	u32 len;
} *rxq;
static u32 rx_head, rx_tail;

void net_register_netif(struct netif *n)
{
	if (!ifs)
		ifs = n;
}
struct netif *net_default_if(void) { return ifs; }

void net_rx(struct netif *n, const void *frame, u32 len)
{
	n->rx_packets++;
	n->rx_bytes += len;
	if (!rxq || rx_head - rx_tail >= RXQ || len > PKT_MAX) {
		n->rx_dropped++;
		return;
	}
	u32 i = rx_head % RXQ;
	memcpy(rxq[i].data, frame, len);
	rxq[i].len = len;
	rx_head++;
	wq_wake_one(&netd_wq);
}

u16 net_checksum(const void *data, u32 len, u32 initial)
{
	u32 sum = initial;
	const u8 *p = data;
	for (u32 i = 0; i + 1 < len; i += 2)
		sum += (u32)(p[i] << 8 | p[i + 1]);
	if (len & 1)
		sum += (u32)(p[len - 1] << 8);
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return (u16)~sum;
}

u32 pseudo_sum(u32 src, u32 dst, u8 proto, u16 len)
{
	return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + proto + len;
}

/* ---- ARP ---- */
#define ARP_N 32
static struct {
	u32 ip;
	u8 mac[6];
	u64 time;
} arp_cache[ARP_N];
static struct {
	u32 nexthop;
	u16 len;
	u8 frame[PKT_MAX];
	u64 time;
} arp_pending[8];

static const u8 bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static bool arp_lookup(u32 ip, u8 *mac)
{
	for (int i = 0; i < ARP_N; i++)
		if (arp_cache[i].ip == ip && arp_cache[i].time) {
			memcpy(mac, arp_cache[i].mac, 6);
			return true;
		}
	return false;
}

static void eth_send(const u8 *dst, u16 type, const void *payload, u32 len)
{
	u8 f[PKT_MAX];
	memcpy(f, dst, 6);
	memcpy(f + 6, ifs->mac, 6);
	f[12] = (u8)(type >> 8);
	f[13] = (u8)type;
	memcpy(f + 14, payload, len);
	u32 total = 14 + len;
	if (total < 60) {
		memset(f + total, 0, 60 - total);
		total = 60;
	}
	ifs->transmit(ifs, f, total);
}

static void arp_request(u32 ip)
{
	u8 p[28];
	p[0] = 0; p[1] = 1;		/* Ethernet */
	p[2] = 0x08; p[3] = 0;		/* IPv4 */
	p[4] = 6; p[5] = 4;
	p[6] = 0; p[7] = 1;		/* request */
	memcpy(p + 8, ifs->mac, 6);
	u32 me = htonl(ifs->ip), tgt = htonl(ip);
	memcpy(p + 14, &me, 4);
	memset(p + 18, 0, 6);
	memcpy(p + 24, &tgt, 4);
	eth_send(bcast, 0x0806, p, 28);
}

static void arp_learn(u32 ip, const u8 *mac)
{
	int slot = 0;
	u64 oldest = ~0ULL;
	for (int i = 0; i < ARP_N; i++) {
		if (arp_cache[i].ip == ip) {
			slot = i;
			break;
		}
		if (arp_cache[i].time < oldest) {
			oldest = arp_cache[i].time;
			slot = i;
		}
	}
	arp_cache[slot].ip = ip;
	memcpy(arp_cache[slot].mac, mac, 6);
	arp_cache[slot].time = time_ns() | 1;
	/* flush frames that were waiting for this address */
	for (int i = 0; i < 8; i++)
		if (arp_pending[i].len && arp_pending[i].nexthop == ip) {
			memcpy(arp_pending[i].frame, mac, 6);
			ifs->transmit(ifs, arp_pending[i].frame, arp_pending[i].len);
			arp_pending[i].len = 0;
		}
}

static void arp_input(const u8 *p, u32 len)
{
	if (len < 28)
		return;
	u32 sip, tip;
	memcpy(&sip, p + 14, 4);
	memcpy(&tip, p + 24, 4);
	sip = ntohl(sip);
	tip = ntohl(tip);
	arp_learn(sip, p + 8);
	if (p[7] == 1 && tip == ifs->ip && ifs->ip) { /* request for us: reply */
		u8 r[28];
		memcpy(r, p, 8);
		r[7] = 2;
		memcpy(r + 8, ifs->mac, 6);
		u32 me = htonl(ifs->ip), them = htonl(sip);
		memcpy(r + 14, &me, 4);
		memcpy(r + 18, p + 8, 6);
		memcpy(r + 24, &them, 4);
		eth_send(p + 8, 0x0806, r, 28);
	}
}

/* ---- IPv4 ---- */
static u16 ip_id;

int ip_send(u32 dst, u8 proto, const void *payload, u32 len)
{
	if (!ifs || len + 20 > ETH_MTU)
		return -E_INVAL;
	u8 pkt[ETH_MTU];
	pkt[0] = 0x45;
	pkt[1] = 0;
	u16 tl = htons((u16)(len + 20)), id = htons(ip_id++);
	memcpy(pkt + 2, &tl, 2);
	memcpy(pkt + 4, &id, 2);
	pkt[6] = 0x40; /* DF */
	pkt[7] = 0;
	pkt[8] = 64;
	pkt[9] = proto;
	pkt[10] = pkt[11] = 0;
	u32 s = htonl(ifs->ip), d = htonl(dst);
	memcpy(pkt + 12, &s, 4);
	memcpy(pkt + 16, &d, 4);
	u16 cs = htons(net_checksum(pkt, 20, 0));
	memcpy(pkt + 10, &cs, 2);
	memcpy(pkt + 20, payload, len);

	u8 mac[6];
	if (dst == ifs->ip || (dst >> 24) == 127) {
		/* local delivery (loopback): re-inject through the receive ring so
		 * the packet is processed by netd like any other */
		if (rxq && rx_head - rx_tail < RXQ) {
			u32 i = rx_head % RXQ;
			memcpy(rxq[i].data, ifs->mac, 6);
			memcpy(rxq[i].data + 6, ifs->mac, 6);
			rxq[i].data[12] = 0x08;
			rxq[i].data[13] = 0x00;
			memcpy(rxq[i].data + 14, pkt, len + 20);
			rxq[i].len = len + 34;
			rx_head++;
			wq_wake_one(&netd_wq);
		}
		return 0;
	}
	if (dst == 0xFFFFFFFF) {
		memcpy(mac, bcast, 6);
	} else {
		u32 hop = ((dst & ifs->netmask) == (ifs->ip & ifs->netmask) || !ifs->gateway) ? dst : ifs->gateway;
		if (!arp_lookup(hop, mac)) {
			/* queue the frame and ask; it is sent when the reply arrives */
			for (int i = 0; i < 8; i++)
				if (!arp_pending[i].len || time_ns() - arp_pending[i].time > 2000000000ULL) {
					memcpy(arp_pending[i].frame + 6, ifs->mac, 6);
					arp_pending[i].frame[12] = 0x08;
					arp_pending[i].frame[13] = 0x00;
					memcpy(arp_pending[i].frame + 14, pkt, len + 20);
					arp_pending[i].len = (u16)MAX(len + 34, 60u);
					if (len + 34 < 60)
						memset(arp_pending[i].frame + len + 34, 0, 60 - (len + 34));
					arp_pending[i].nexthop = hop;
					arp_pending[i].time = time_ns();
					break;
				}
			arp_request(hop);
			return 0;
		}
	}
	eth_send(mac, 0x0800, pkt, len + 20);
	return 0;
}

/* ---- ICMP ---- */
static void icmp_input(u32 src, u8 *p, u32 len)
{
	if (len < 8 || net_checksum(p, len, 0) != 0)
		return;
	if (p[0] == 8) { /* echo request -> reply */
		p[0] = 0;
		p[2] = p[3] = 0;
		u16 cs = htons(net_checksum(p, len, 0));
		memcpy(p + 2, &cs, 2);
		ip_send(src, 1, p, len);
	} else if (p[0] == 0) {
		sock_icmp_input(src, p, len);
	}
}

/* ---- UDP ---- */
int udp_send(u32 dst, u16 sport, u16 dport, const void *data, u32 len)
{
	u8 p[ETH_MTU];
	if (len + 8 > ETH_MTU - 20)
		return -E_INVAL;
	u16 sp = htons(sport), dp = htons(dport), l = htons((u16)(len + 8));
	memcpy(p, &sp, 2);
	memcpy(p + 2, &dp, 2);
	memcpy(p + 4, &l, 2);
	p[6] = p[7] = 0;
	memcpy(p + 8, data, len);
	u16 cs = net_checksum(p, len + 8, pseudo_sum(ifs->ip, dst, 17, (u16)(len + 8)));
	cs = htons(cs ? cs : 0xFFFF);
	memcpy(p + 6, &cs, 2);
	return ip_send(dst, 17, p, len + 8);
}

static void dhcp_input(const u8 *p, u32 len);

static void udp_input(u32 src, const u8 *p, u32 len)
{
	if (len < 8)
		return;
	u16 sport = (u16)(p[0] << 8 | p[1]), dport = (u16)(p[2] << 8 | p[3]);
	if (dport == 68) {
		dhcp_input(p + 8, len - 8);
		return;
	}
	sock_udp_input(src, sport, dport, p + 8, len - 8);
}

static void ip_input(u8 *p, u32 len)
{
	if (len < 20 || (p[0] >> 4) != 4)
		return;
	u32 ihl = (p[0] & 0xF) * 4;
	u32 tl = (u32)(p[2] << 8 | p[3]);
	if (tl > len || ihl < 20 || net_checksum(p, ihl, 0) != 0)
		return;
	if ((p[6] & 0x3F) || p[7]) /* fragments are not supported */
		return;
	u32 src, dst;
	memcpy(&src, p + 12, 4);
	memcpy(&dst, p + 16, 4);
	src = ntohl(src);
	dst = ntohl(dst);
	if (ifs->ip && dst != ifs->ip && dst != 0xFFFFFFFF)
		return;
	switch (p[9]) {
	case 1: icmp_input(src, p + ihl, tl - ihl); break;
	case 6: tcp_input(src, dst, p + ihl, tl - ihl); break;
	case 17: udp_input(src, p + ihl, tl - ihl); break;
	}
}

static void eth_input(u8 *f, u32 len)
{
	if (len < 14)
		return;
	u16 type = (u16)(f[12] << 8 | f[13]);
	if (type == 0x0806)
		arp_input(f + 14, len - 14);
	else if (type == 0x0800)
		ip_input(f + 14, len - 14);
}

/* ---- DHCP (client, used once at boot) ---- */
static u32 dhcp_xid;
static int dhcp_state; /* 0 idle, 1 discover sent, 2 request sent, 3 bound */
static u32 dhcp_offer, dhcp_server;

static void dhcp_send(int type)
{
	u8 m[300];
	memset(m, 0, sizeof(m));
	m[0] = 1; m[1] = 1; m[2] = 6;
	u32 x = htonl(dhcp_xid);
	memcpy(m + 4, &x, 4);
	m[10] = 0x80; /* broadcast reply */
	memcpy(m + 28, ifs->mac, 6);
	m[236] = 99; m[237] = 130; m[238] = 83; m[239] = 99;
	u8 *o = m + 240;
	*o++ = 53; *o++ = 1; *o++ = (u8)type;
	if (type == 3) {
		u32 req = htonl(dhcp_offer), srv = htonl(dhcp_server);
		*o++ = 50; *o++ = 4; memcpy(o, &req, 4); o += 4;
		*o++ = 54; *o++ = 4; memcpy(o, &srv, 4); o += 4;
	}
	*o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6; /* mask, router, dns */
	*o++ = 255;
	udp_send(0xFFFFFFFF, 68, 67, m, (u32)(o - m));
}

static void dhcp_input(const u8 *m, u32 len)
{
	if (len < 240)
		return;
	u32 x;
	memcpy(&x, m + 4, 4);
	if (ntohl(x) != dhcp_xid)
		return;
	u32 yi;
	memcpy(&yi, m + 16, 4);
	int type = 0;
	u32 mask = 0, router = 0, dns = 0, server = 0;
	for (const u8 *o = m + 240; o < m + len && *o != 255;) {
		if (*o == 0) { o++; continue; }
		u8 code = o[0], l = o[1];
		u32 v = 0;
		if (l >= 4)
			memcpy(&v, o + 2, 4);
		switch (code) {
		case 53: type = o[2]; break;
		case 1: mask = ntohl(v); break;
		case 3: router = ntohl(v); break;
		case 6: dns = ntohl(v); break;
		case 54: server = ntohl(v); break;
		}
		o += 2 + l;
	}
	if (type == 2 && dhcp_state == 1) {
		dhcp_offer = ntohl(yi);
		dhcp_server = server;
		dhcp_state = 2;
		dhcp_send(3);
	} else if (type == 5 && dhcp_state == 2) {
		ifs->ip = ntohl(yi);
		ifs->netmask = mask ? mask : IP4(255, 255, 255, 0);
		ifs->gateway = router;
		ifs->dns = dns ? dns : router;
		ifs->up = true;
		dhcp_state = 3;
		KLOG("net", "DHCP: %u.%u.%u.%u/%u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u",
		     ifs->ip >> 24, (ifs->ip >> 16) & 255, (ifs->ip >> 8) & 255, ifs->ip & 255,
		     ifs->netmask >> 24, (ifs->netmask >> 16) & 255, (ifs->netmask >> 8) & 255,
		     ifs->netmask & 255, ifs->gateway >> 24, (ifs->gateway >> 16) & 255,
		     (ifs->gateway >> 8) & 255, ifs->gateway & 255, ifs->dns >> 24,
		     (ifs->dns >> 16) & 255, (ifs->dns >> 8) & 255, ifs->dns & 255);
	}
}

/* ---- the network thread ---- */
static void netd(void *arg)
{
	u64 dhcp_start = time_ns();
	dhcp_xid = (u32)rdtsc();
	dhcp_state = 1;
	dhcp_send(1);
	int attempts = 1;
	for (;;) {
		u64 f = irq_save();
		while (rx_head == rx_tail)
			if (wq_wait(&netd_wq, 10000000ULL) == -E_TIMEDOUT)
				break;
		/* protocol state is shared with socket syscalls: process with
		 * interrupts off (the uniprocessor kernel's lock) */
		while (rx_tail != rx_head) {
			u32 i = rx_tail % RXQ;
			eth_input(rxq[i].data, rxq[i].len);
			rx_tail++;
		}
		tcp_timer();
		irq_restore(f);
		if (dhcp_state != 3 && time_ns() - dhcp_start > 1500000000ULL) {
			if (attempts < 3) {
				attempts++;
				dhcp_start = time_ns();
				dhcp_state = 1;
				dhcp_send(1);
			} else if (!ifs->up) {
				/* QEMU user networking defaults */
				ifs->ip = IP4(10, 0, 2, 15);
				ifs->netmask = IP4(255, 255, 255, 0);
				ifs->gateway = IP4(10, 0, 2, 2);
				ifs->dns = IP4(10, 0, 2, 3);
				ifs->up = true;
				dhcp_state = 3;
				KLOG("net", "DHCP gave no answer; static 10.0.2.15/24 (QEMU defaults)");
			}
		}
	}
}

struct pbuf;
void pb_printf(struct pbuf *p, const char *fmt, ...);
void procfs_register(const char *name, void (*gen)(void *p));
static void gen_net(void *pb)
{
	if (!ifs) {
		pb_printf(pb, "no network interface\n");
		return;
	}
	pb_printf(pb, "%s %s mac %02x:%02x:%02x:%02x:%02x:%02x ip %u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u\n",
		  ifs->name, ifs->up ? "UP" : "DOWN", ifs->mac[0], ifs->mac[1], ifs->mac[2], ifs->mac[3],
		  ifs->mac[4], ifs->mac[5], ifs->ip >> 24, (ifs->ip >> 16) & 255, (ifs->ip >> 8) & 255,
		  ifs->ip & 255, ifs->gateway >> 24, (ifs->gateway >> 16) & 255,
		  (ifs->gateway >> 8) & 255, ifs->gateway & 255, ifs->dns >> 24, (ifs->dns >> 16) & 255,
		  (ifs->dns >> 8) & 255, ifs->dns & 255);
	pb_printf(pb, "rx_packets %lu rx_bytes %lu tx_packets %lu tx_bytes %lu dropped %lu\n",
		  ifs->rx_packets, ifs->rx_bytes, ifs->tx_packets, ifs->tx_bytes, ifs->rx_dropped);
	tcp_dump(pb);
}

void net_fill_info(struct fu_netinfo *ni)
{
	memset(ni, 0, sizeof(*ni));
	if (!ifs)
		return;
	memcpy(ni->mac, ifs->mac, 6);
	ni->up = ifs->up;
	ni->ip = htonl(ifs->ip);
	ni->gateway = htonl(ifs->gateway);
	ni->netmask = htonl(ifs->netmask);
	ni->dns = htonl(ifs->dns);
	ni->rx_packets = ifs->rx_packets;
	ni->tx_packets = ifs->tx_packets;
	ni->rx_bytes = ifs->rx_bytes;
	ni->tx_bytes = ifs->tx_bytes;
	ni->rx_dropped = ifs->rx_dropped;
	for (int i = 0; i < ARP_N; i++)
		ni->arp_entries += arp_cache[i].time != 0;
	tcp_counts(&ni->tcp_active, &ni->udp_active);
}

void net_init_devices(void)
{
	wq_init(&netd_wq);
	rxq = vmalloc_pages(ALIGN_UP(sizeof(*rxq) * RXQ, PAGE_SIZE) / PAGE_SIZE);
	procfs_register("net", (void (*)(void *))gen_net);
	if (!ifs) {
		KLOG("net", "no network interface found");
		return;
	}
	sock_init();
	task_create_kernel("netd", netd, NULL);
}
