/* FuhrerOS network stack: virtio-net -> Ethernet -> ARP / IPv4 -> ICMP,
 * UDP, TCP -> sockets (NEW_EXPLANATION §29). */
#ifndef FUHRER_NET_H
#define FUHRER_NET_H
#include "types.h"

#define ETH_MTU 1500
#define ETH_HLEN 14
#define PKT_MAX 1600

struct netif {
	char name[8];
	u8 mac[6];
	u32 ip, netmask, gateway, dns;	/* host byte order */
	bool up;
	int (*transmit)(struct netif *n, const void *frame, u32 len);
	void *priv;
	u64 rx_packets, tx_packets, rx_bytes, tx_bytes, rx_dropped;
};

void net_register_netif(struct netif *n);
/* Called by drivers (may be IRQ context): copies the frame to the rx queue. */
void net_rx(struct netif *n, const void *frame, u32 len);
struct netif *net_default_if(void);

static inline u16 bswap16(u16 v) { return (u16)((v >> 8) | (v << 8)); }
static inline u32 bswap32(u32 v)
{
	return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}
#define htons bswap16
#define ntohs bswap16
#define htonl bswap32
#define ntohl bswap32
#define IP4(a, b, c, d) (((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(c) << 8) | (u32)(d))

/* internal: used by the socket layer */
int ip_send(u32 dst, u8 proto, const void *payload, u32 len);
int udp_send(u32 dst, u16 sport, u16 dport, const void *data, u32 len);
#endif
