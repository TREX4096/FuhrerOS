/* ping HOST [COUNT] - ICMP echo */
#include "fu.h"

static uint16_t csum(const uint8_t *p, int n)
{
	uint32_t s = 0;
	for (int i = 0; i + 1 < n; i += 2)
		s += (uint32_t)(p[i] << 8 | p[i + 1]);
	if (n & 1)
		s += (uint32_t)(p[n - 1] << 8);
	while (s >> 16)
		s = (s & 0xFFFF) + (s >> 16);
	return (uint16_t)~s;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		dprintf(2, "usage: ping HOST [COUNT]\n");
		return 2;
	}
	int count = argc > 2 ? atoi(argv[2]) : 4;
	uint32_t ip;
	int r = resolve(argv[1], &ip);
	if (r < 0) {
		dprintf(2, "ping: %s: %s\n", argv[1], strerror(r));
		return 1;
	}
	int fd = socket(AF_INET, SOCK_ICMP);
	if (fd < 0) {
		dprintf(2, "ping: socket: %s\n", strerror(fd));
		return 1;
	}
	char ips[16];
	format_ip(ip, ips);
	printf("PING %s (%s): 32 data bytes\n", argv[1], ips);
	uint16_t id = (uint16_t)(getpid() + 0x4600);
	int received = 0;
	for (int seq = 1; seq <= count; seq++) {
		uint8_t pkt[40] = { 8, 0, 0, 0, (uint8_t)(id >> 8), (uint8_t)id,
				    (uint8_t)(seq >> 8), (uint8_t)seq };
		for (int i = 8; i < 40; i++)
			pkt[i] = (uint8_t)i;
		uint16_t c = csum(pkt, 40);
		pkt[2] = (uint8_t)(c >> 8);
		pkt[3] = (uint8_t)c;
		uint64_t t0 = uptime_ns();
		sendto(fd, pkt, sizeof(pkt), ip, 0);
		uint8_t rep[64];
		ssize_t n = recvfrom(fd, rep, sizeof(rep), NULL, 1000);
		if (n >= 8 && rep[6] == (uint8_t)(seq >> 8) && rep[7] == (uint8_t)seq) {
			uint64_t us = (uptime_ns() - t0) / 1000;
			printf("%ld bytes from %s: icmp_seq=%d time=%lu.%03lu ms\n", (long)n, ips, seq,
			       us / 1000, us % 1000);
			received++;
		} else {
			printf("request icmp_seq=%d timed out\n", seq);
		}
		if (seq < count)
			sleep_ms(500);
	}
	printf("--- %s: %d/%d received ---\n", argv[1], received, count);
	return received ? 0 : 1;
}
