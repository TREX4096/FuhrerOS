/* fetch URL [-o FILE] [-q] [-i] — HTTP/1.0 client (http:// only; TLS is not
 * implemented). Prints the body, or saves it with -o; -i shows headers. */
#include "fu.h"

int main(int argc, char **argv)
{
	const char *url = NULL, *out = NULL;
	bool quiet = false, headers = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o") && i + 1 < argc)
			out = argv[++i];
		else if (!strcmp(argv[i], "-q"))
			quiet = true;
		else if (!strcmp(argv[i], "-i"))
			headers = true;
		else
			url = argv[i];
	}
	if (!url) {
		dprintf(2, "usage: fetch http://HOST[:PORT]/PATH [-o FILE] [-q] [-i]\n");
		return 2;
	}
	if (!strncmp(url, "https://", 8)) {
		dprintf(2, "fetch: https is not supported yet (no TLS); try http://\n");
		return 2;
	}
	if (!strncmp(url, "http://", 7))
		url += 7;
	char host[128];
	const char *slash = strchr(url, '/');
	size_t hl = slash ? (size_t)(slash - url) : strlen(url);
	if (hl >= sizeof(host))
		return 2;
	memcpy(host, url, hl);
	host[hl] = 0;
	const char *path = slash ? slash : "/";
	uint16_t port = 80;
	char *colon = strchr(host, ':');
	if (colon) {
		*colon = 0;
		port = (uint16_t)atoi(colon + 1);
	}
	uint32_t ip;
	int r = resolve(host, &ip);
	if (r < 0) {
		dprintf(2, "fetch: %s: %s\n", host, strerror(r));
		return 1;
	}
	int fd = socket(AF_INET, SOCK_STREAM);
	uint64_t t0 = uptime_ns();
	r = connect(fd, ip, port);
	if (r < 0) {
		dprintf(2, "fetch: connect %s:%u: %s\n", host, port, strerror(r));
		return 1;
	}
	char req[512];
	int n = snprintf(req, sizeof(req),
			 "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: FuhrerOS-fetch/0.1\r\n"
			 "Connection: close\r\n\r\n",
			 path, host);
	send(fd, req, (size_t)n);
	int ofd = out ? open(out, O_WRONLY | O_CREAT | O_TRUNC) : 1;
	static char buf[8192];
	size_t total = 0, body = 0;
	bool in_body = false;
	char tail[4] = { 0 };
	int status = 0;
	ssize_t k;
	while ((k = recv(fd, buf, sizeof(buf))) > 0) {
		ssize_t start = 0;
		if (!in_body) {
			if (total == 0 && k > 12)
				status = atoi(buf + 9);
			for (ssize_t i = 0; i < k; i++) {
				tail[0] = tail[1];
				tail[1] = tail[2];
				tail[2] = tail[3];
				tail[3] = buf[i];
				if (!memcmp(tail, "\r\n\r\n", 4)) {
					in_body = true;
					start = i + 1;
					if (headers)
						write(2, buf, (size_t)start);
					break;
				}
			}
			if (!in_body) {
				if (headers)
					write(2, buf, (size_t)k);
				total += (size_t)k;
				continue;
			}
		}
		total += (size_t)k;
		body += (size_t)(k - start);
		if (!quiet || out)
			write(ofd, buf + start, (size_t)(k - start));
	}
	close(fd);
	if (out)
		close(ofd);
	uint64_t ms = (uptime_ns() - t0) / 1000000;
	dprintf(2, "fetch: HTTP %d, %lu body bytes in %lu ms\n", status, (unsigned long)body, ms);
	return status >= 200 && status < 400 ? 0 : 1;
}
