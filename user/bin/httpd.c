/* httpd [PORT] [ROOT] — tiny static HTTP/1.0 server (default port 80,
 * root /www). QEMU forwards host 127.0.0.1:8080 to guest port 80. */
#include "fu.h"

static void serve(int c, const char *root)
{
	char req[1024];
	ssize_t n = recv(c, req, sizeof(req) - 1);
	if (n <= 0)
		return;
	req[n] = 0;
	char path[256] = "/";
	if (!strncmp(req, "GET ", 4)) {
		char *sp = strchr(req + 4, ' ');
		if (sp)
			*sp = 0;
		strlcpy(path, req + 4, sizeof(path));
	}
	if (strstr(path, ".."))
		strlcpy(path, "/", sizeof(path));
	char file[320];
	snprintf(file, sizeof(file), "%s%s%s", root, path,
		 path[strlen(path) - 1] == '/' ? "index.html" : "");
	int fd = open(file, O_RDONLY);
	char hdr[256];
	if (fd < 0) {
		const char *msg = "<h1>404 Not Found</h1><p>FuhrerOS httpd</p>\n";
		int h = snprintf(hdr, sizeof(hdr),
				 "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n"
				 "Content-Length: %lu\r\n\r\n",
				 (unsigned long)strlen(msg));
		send(c, hdr, (size_t)h);
		send(c, msg, strlen(msg));
		printf("httpd: GET %s -> 404\n", path);
		return;
	}
	struct stat st;
	fstat(fd, &st);
	const char *type = strstr(file, ".html") ? "text/html" : "text/plain";
	int h = snprintf(hdr, sizeof(hdr),
			 "HTTP/1.0 200 OK\r\nServer: FuhrerOS-httpd\r\nContent-Type: %s\r\n"
			 "Content-Length: %lu\r\n\r\n",
			 type, st.size);
	send(c, hdr, (size_t)h);
	static char buf[8192];
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		send(c, buf, (size_t)n);
	close(fd);
	printf("httpd: GET %s -> 200 (%lu bytes)\n", path, st.size);
}

int main(int argc, char **argv)
{
	int port = argc > 1 ? atoi(argv[1]) : 80;
	const char *root = argc > 2 ? argv[2] : "/www";
	int s = socket(AF_INET, SOCK_STREAM);
	int r = bind(s, (uint16_t)port);
	if (r < 0 || (r = listen(s, 8)) < 0) {
		dprintf(2, "httpd: cannot listen on %d: %s\n", port, strerror(r));
		return 1;
	}
	printf("httpd: serving %s on port %d\n", root, port);
	for (;;) {
		struct sockaddr_in from;
		int c = accept(s, &from);
		if (c < 0)
			continue;
		serve(c, root);
		close(c);
	}
}
