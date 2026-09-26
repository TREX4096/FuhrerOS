/* nettest - network acceptance tests (run by usertest): interface up via
 * DHCP, TCP through FuhrerOS's own stack, DNS, and HTTP to the internet. */
#include "fu.h"

static int fails;

static void rep(const char *n, bool ok, const char *d)
{
	printf("NTEST %-24s %s %s\n", n, ok ? "PASS" : "FAIL", d ? d : "");
	fails += !ok;
}

static void echo_server(void *arg)
{
	int s = *(int *)arg;
	int c = accept(s, NULL);
	char buf[4096];
	ssize_t n;
	while ((n = recv(c, buf, sizeof(buf))) > 0)
		send(c, buf, (size_t)n);
	close(c);
}

int main(void)
{
	char d[128];
	struct fu_netinfo ni;
	for (int i = 0; i < 50 && (netinfo(&ni) < 0 || !ni.up); i++)
		sleep_ms(100);
	char ip[16];
	format_ip(ni.ip, ip);
	rep("net.interface_up", ni.up && ni.ip, ip);

	/* TCP to our own address (kernel loopback path): exercises the
	 * handshake, windows and FIN handling of both ends of the stack. */
	int ls = socket(AF_INET, SOCK_STREAM);
	bind(ls, 5555);
	listen(ls, 2);
	thread_create(echo_server, &ls, 0);
	int c = socket(AF_INET, SOCK_STREAM);
	int r = connect(c, ni.ip, 5555);
	static char big[20000], back[20000];
	for (int i = 0; i < (int)sizeof(big); i++)
		big[i] = (char)('a' + i % 26);
	size_t got = 0;
	if (r == 0) {
		send(c, big, sizeof(big));
		while (got < sizeof(big)) {
			ssize_t n = recv(c, back + got, sizeof(back) - got);
			if (n <= 0)
				break;
			got += (size_t)n;
		}
	}
	close(c);
	snprintf(d, sizeof(d), "connect=%d, %lu/20000 bytes echoed", r, (unsigned long)got);
	rep("net.tcp_self_echo", r == 0 && got == sizeof(big) && !memcmp(big, back, sizeof(big)), d);

	uint32_t eip = 0;
	r = resolve("example.com", &eip);
	format_ip(eip, ip);
	rep("net.dns_example_com", r == 0, r == 0 ? ip : strerror(r));
	if (r == 0) {
		const char *argv[] = { "/bin/fetch", "http://example.com/", "-o", "/tmp/example.html", NULL };
		pid_t p = spawn("/bin/fetch", argv, NULL);
		int st = 1;
		if (p > 0)
			wait(p, &st);
		static char page[8192];
		read_file("/tmp/example.html", page, sizeof(page));
		rep("net.http_example_com", st == 0 && strstr(page, "Example Domain"),
		    "fetch http://example.com/");
	}
	return fails;
}
