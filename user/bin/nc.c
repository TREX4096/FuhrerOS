/* nc HOST PORT   — connect, send stdin, print replies
 * nc -l PORT     — TCP echo server (one connection at a time) */
#include "fu.h"

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "-l")) {
		int s = socket(AF_INET, SOCK_STREAM);
		if (bind(s, (uint16_t)atoi(argv[2])) < 0 || listen(s, 4) < 0) {
			dprintf(2, "nc: cannot listen\n");
			return 1;
		}
		printf("nc: echo server on port %s\n", argv[2]);
		for (;;) {
			int c = accept(s, NULL);
			char buf[2048];
			ssize_t n;
			while ((n = recv(c, buf, sizeof(buf))) > 0)
				send(c, buf, (size_t)n);
			close(c);
		}
	}
	if (argc < 3) {
		dprintf(2, "usage: nc HOST PORT | nc -l PORT\n");
		return 2;
	}
	uint32_t ip;
	if (resolve(argv[1], &ip) < 0)
		return 1;
	int s = socket(AF_INET, SOCK_STREAM);
	int r = connect(s, ip, (uint16_t)atoi(argv[2]));
	if (r < 0) {
		dprintf(2, "nc: %s\n", strerror(r));
		return 1;
	}
	char buf[2048];
	for (;;) {
		int fds[2] = { 0, s };
		int m = poll_readable(fds, 2, 0);
		if (m & 1) {
			ssize_t n = read(0, buf, sizeof(buf));
			if (n <= 0)
				break;
			send(s, buf, (size_t)n);
		}
		if (m & 2) {
			ssize_t n = recv(s, buf, sizeof(buf));
			if (n <= 0)
				break;
			write(1, buf, (size_t)n);
		}
	}
	close(s);
	return 0;
}
