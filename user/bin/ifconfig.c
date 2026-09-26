/* ifconfig — network interface status */
#include "fu.h"

int main(void)
{
	struct fu_netinfo ni;
	if (netinfo(&ni) < 0) {
		dprintf(2, "ifconfig: no network\n");
		return 1;
	}
	char ip[16], gw[16], mask[16], dns[16];
	format_ip(ni.ip, ip);
	format_ip(ni.gateway, gw);
	format_ip(ni.netmask, mask);
	format_ip(ni.dns, dns);
	printf("eth0: %s  mac %02x:%02x:%02x:%02x:%02x:%02x\n", ni.up ? "UP" : "DOWN", ni.mac[0],
	       ni.mac[1], ni.mac[2], ni.mac[3], ni.mac[4], ni.mac[5]);
	printf("      inet %s  netmask %s  gateway %s  dns %s\n", ip, mask, gw, dns);
	printf("      rx %lu packets (%lu bytes)  tx %lu packets (%lu bytes)  dropped %lu\n",
	       ni.rx_packets, ni.rx_bytes, ni.tx_packets, ni.tx_bytes, ni.rx_dropped);
	printf("      tcp connections %u  udp sockets %u  arp entries %u\n", ni.tcp_active,
	       ni.udp_active, ni.arp_entries);
	return 0;
}
