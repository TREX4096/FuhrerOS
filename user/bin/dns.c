/* dns NAME — resolve a host name through the DNS server from DHCP */
#include "fu.h"

int main(int argc, char **argv)
{
	if (argc < 2) {
		dprintf(2, "usage: dns NAME\n");
		return 2;
	}
	uint32_t ip;
	uint64_t t0 = uptime_ns();
	int r = resolve(argv[1], &ip);
	if (r < 0) {
		dprintf(2, "dns: %s: %s\n", argv[1], strerror(r));
		return 1;
	}
	char s[16];
	format_ip(ip, s);
	printf("%s has address %s (%lu ms)\n", argv[1], s, (uptime_ns() - t0) / 1000000);
	return 0;
}
