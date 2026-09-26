/* libfu networking: socket wrappers and a DNS (A record) resolver. */
#include "fu.h"

int socket(int d, int t) { return (int)sys2(SYS_SOCKET, d, t); }
int connect(int fd, uint32_t ip, uint16_t port)
{
	struct sockaddr_in sa = { AF_INET, htons(port), ip, { 0 } };
	return (int)sys2(SYS_CONNECT, fd, &sa);
}
int bind(int fd, uint16_t port)
{
	struct sockaddr_in sa = { AF_INET, htons(port), 0, { 0 } };
	return (int)sys2(SYS_BIND, fd, &sa);
}
int listen(int fd, int backlog) { return (int)sys2(SYS_LISTEN, fd, backlog); }
int accept(int fd, struct sockaddr_in *from) { return (int)sys2(SYS_ACCEPT, fd, from); }
ssize_t send(int fd, const void *b, size_t n) { return sys3(SYS_SEND, fd, b, n); }
ssize_t recv(int fd, void *b, size_t n) { return sys3(SYS_RECV, fd, b, n); }
ssize_t sendto(int fd, const void *b, size_t n, uint32_t ip, uint16_t port)
{
	struct sockaddr_in sa = { AF_INET, htons(port), ip, { 0 } };
	return sys4(SYS_SENDTO, fd, b, n, &sa);
}
ssize_t recvfrom(int fd, void *b, size_t n, struct sockaddr_in *from, uint64_t t)
{
	return sys5(SYS_RECVFROM, fd, b, n, from, t);
}
int netinfo(struct fu_netinfo *ni) { return (int)sys1(SYS_NETINFO, ni); }

int parse_ip(const char *s, uint32_t *ip)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) {
		if (!isdigit(*s))
			return -1;
		long o = strtol(s, (char **)&s, 10);
		if (o > 255 || (i < 3 && *s++ != '.'))
			return -1;
		v = (v << 8) | (uint32_t)o;
	}
	if (*s)
		return -1;
	*ip = htonl(v);
	return 0;
}

void format_ip(uint32_t ip, char *out)
{
	uint32_t h = ntohl(ip);
	snprintf(out, 16, "%u.%u.%u.%u", h >> 24, (h >> 16) & 255, (h >> 8) & 255, h & 255);
}

int resolve(const char *host, uint32_t *ip)
{
	if (parse_ip(host, ip) == 0)
		return 0;
	if (!strcmp(host, "localhost")) {
		struct fu_netinfo ni;
		netinfo(&ni);
		*ip = ni.ip;
		return 0;
	}
	struct fu_netinfo ni;
	if (netinfo(&ni) < 0 || !ni.up)
		return -101;
	int fd = socket(AF_INET, SOCK_DGRAM);
	if (fd < 0)
		return fd;
	uint8_t q[512];
	memset(q, 0, 12);
	uint16_t id = (uint16_t)rand32();
	q[0] = (uint8_t)(id >> 8);
	q[1] = (uint8_t)id;
	q[2] = 0x01; /* recursion desired */
	q[5] = 1;    /* one question */
	size_t n = 12;
	const char *p = host;
	while (*p) {
		const char *dot = strchr(p, '.');
		size_t l = dot ? (size_t)(dot - p) : strlen(p);
		if (l == 0 || l > 63 || n + l + 6 > sizeof(q)) {
			close(fd);
			return -22;
		}
		q[n++] = (uint8_t)l;
		memcpy(q + n, p, l);
		n += l;
		p += l + (dot ? 1 : 0);
		if (!dot)
			break;
	}
	q[n++] = 0;
	q[n++] = 0; q[n++] = 1; /* A */
	q[n++] = 0; q[n++] = 1; /* IN */
	int rc = -110;
	for (int attempt = 0; attempt < 3 && rc == -110; attempt++) {
		sendto(fd, q, n, ni.dns, 53);
		uint8_t r[512];
		ssize_t got = recvfrom(fd, r, sizeof(r), NULL, 1500);
		if (got < 12 || r[0] != q[0] || r[1] != q[1])
			continue;
		int ancount = r[6] << 8 | r[7];
		size_t off = n; /* skip header + question (same as ours) */
		for (int i = 0; i < ancount && off + 12 <= (size_t)got; i++) {
			if ((r[off] & 0xC0) == 0xC0)
				off += 2;
			else {
				while (off < (size_t)got && r[off])
					off += r[off] + 1;
				off++;
			}
			uint16_t type = (uint16_t)(r[off] << 8 | r[off + 1]);
			uint16_t rdlen = (uint16_t)(r[off + 8] << 8 | r[off + 9]);
			off += 10;
			if (type == 1 && rdlen == 4) {
				memcpy(ip, r + off, 4);
				rc = 0;
				break;
			}
			off += rdlen;
		}
		if (rc != 0)
			rc = -2; /* answered, but no A record */
	}
	close(fd);
	return rc;
}
