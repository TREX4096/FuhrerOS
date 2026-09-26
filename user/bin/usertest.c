/* usertest — user-space acceptance tests (M5, M6, M8 and later milestones).
 * Prints "UTEST <name> PASS|FAIL <detail>" lines; exit status = #failures. */
#include "fu.h"

static int failures;

static void report(const char *name, bool ok, const char *detail)
{
	printf("UTEST %-26s %s %s\n", name, ok ? "PASS" : "FAIL", detail ? detail : "");
	if (!ok)
		failures++;
}

static int run(const char *path, const char *a1, const char *a2, int out_fd)
{
	const char *argv[] = { path, a1, a2, NULL };
	int stdio[3] = { 0, out_fd >= 0 ? out_fd : 1, 2 };
	pid_t p = spawn(path, argv, stdio);
	if (p < 0)
		return -1000 + p;
	int st = -1;
	wait(p, &st);
	return st;
}

static volatile int thread_hits;
static void thread_fn(void *arg)
{
	for (int i = 0; i < 1000; i++)
		thread_hits++;
	(void)arg;
}

int main(int argc, char **argv)
{
	char buf[512], detail[128];

	/* --- privilege boundary (M5) --- */
	if (argc > 1 && !strcmp(argv[1], "--privileged")) {
		__asm__ volatile("cli"); /* must #GP in ring 3 */
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "--kernel-read")) {
		volatile uint64_t v = *(volatile uint64_t *)0xffffffff80000000ULL;
		return (int)v;
	}
	report("process.ring3", true, "usertest itself runs in ring 3");
	int st = run("/bin/usertest", "--privileged", NULL, -1);
	snprintf(detail, sizeof(detail), "cli in ring 3 -> exit %d (128+13=#GP expected)", st);
	report("process.privileged_insn", st == 128 + 13, detail);
	st = run("/bin/usertest", "--kernel-read", NULL, -1);
	snprintf(detail, sizeof(detail), "kernel address read -> exit %d (128+14=#PF expected)", st);
	report("process.kernel_memory", st == 128 + 14, detail);

	/* --- spawn/wait + exit codes --- */
	st = run("/bin/sh", "-c", "exit 7", -1);
	report("process.spawn_wait", st == 7, "sh -c 'exit 7' returned 7");

	/* --- pipes: parent reads child's stdout --- */
	int p[2];
	pipe(p);
	const char *eargv[] = { "/bin/echo", "hello", "pipes", NULL };
	int stdio[3] = { 0, p[1], 2 };
	pid_t pid = spawn("/bin/echo", eargv, stdio);
	close(p[1]);
	ssize_t n = readline(p[0], buf, sizeof(buf));
	close(p[0]);
	wait(pid, NULL);
	report("ipc.pipe", n > 0 && !strcmp(buf, "hello pipes\n"), buf);

	/* --- files (M8) on the root filesystem --- */
	mkdir("/home/ut");
	int fd = open("/home/ut/a.txt", O_WRONLY | O_CREAT | O_TRUNC);
	const char *msg = "FuhrerOS file test\n";
	bool ok = fd >= 0 && write(fd, msg, strlen(msg)) == (ssize_t)strlen(msg);
	close(fd);
	struct stat sst;
	ok &= stat("/home/ut/a.txt", &sst) == 0 && sst.size == strlen(msg);
	report("fs.create_write_stat", ok, NULL);
	ok = run("/bin/cp", "/home/ut/a.txt", "/home/ut/b.txt", -1) == 0;
	ok &= read_file("/home/ut/b.txt", buf, sizeof(buf)) > 0 && !strcmp(buf, msg);
	report("fs.cp", ok, NULL);
	ok = run("/bin/mv", "/home/ut/b.txt", "/home/ut/c.txt", -1) == 0;
	ok &= stat("/home/ut/b.txt", &sst) < 0 && stat("/home/ut/c.txt", &sst) == 0;
	report("fs.mv", ok, NULL);
	ok = run("/bin/rm", "/home/ut/c.txt", NULL, -1) == 0 && stat("/home/ut/c.txt", &sst) < 0;
	report("fs.rm", ok, NULL);
	/* big file: several blocks + indirect blocks on FFS0 */
	fd = open("/home/ut/big.bin", O_RDWR | O_CREAT | O_TRUNC);
	static uint8_t blk[8192];
	uint32_t sum_w = 0, sum_r = 0;
	for (int i = 0; i < 128; i++) { /* 1 MiB */
		for (int j = 0; j < 8192; j++)
			blk[j] = (uint8_t)(i * 31 + j);
		for (int j = 0; j < 8192; j++)
			sum_w += blk[j];
		write(fd, blk, sizeof(blk));
	}
	lseek(fd, 0, SEEK_SET);
	while ((n = read(fd, blk, sizeof(blk))) > 0)
		for (ssize_t j = 0; j < n; j++)
			sum_r += blk[j];
	close(fd);
	snprintf(detail, sizeof(detail), "1 MiB written/read, checksum %u/%u", sum_w, sum_r);
	report("fs.large_file", sum_w == sum_r, detail);
	unlink("/home/ut/big.bin");
	unlink("/home/ut/a.txt");
	ok = unlink("/home/ut") == 0;
	report("fs.rmdir", ok, NULL);

	/* --- shell pipeline + redirection (M6) --- */
	st = run("/bin/sh", "-c", "echo alpha beta gamma | wc > /tmp/wc.txt", -1);
	read_file("/tmp/wc.txt", buf, sizeof(buf));
	report("shell.pipeline_redirect", st == 0 && strstr(buf, "      1       3      17"), buf);

	/* --- threads --- */
	thread_hits = 0;
	int t1 = thread_create(thread_fn, NULL, 0), t2 = thread_create(thread_fn, NULL, 0);
	for (int i = 0; i < 100 && thread_hits < 2000; i++)
		sleep_ms(5);
	snprintf(detail, sizeof(detail), "tids %d %d, hits %d", t1, t2, thread_hits);
	report("process.threads", t1 > 0 && t2 > 0 && thread_hits >= 2000, detail);

	/* --- message ports --- */
	int port = port_create("utest");
	ok = port >= 0 && port_send(port, "ping", 5) == 0;
	pid_t sender = 0;
	n = port_recv(port, buf, sizeof(buf), &sender, 100);
	report("ipc.port", ok && n == 5 && !strcmp(buf, "ping") && sender == getpid(), NULL);

	/* --- procfs / sysinfo --- */
	struct fu_sysinfo si;
	sysinfo(&si);
	snprintf(detail, sizeof(detail), "%lu MiB total, %u procs, policy %s", si.mem_total_kb / 1024,
		 si.procs, si.policy);
	report("sys.sysinfo", si.mem_total_kb > 0 && si.procs >= 2, detail);

	/* optional later-milestone tests live in separate programs */
	struct stat tst;
	if (stat("/bin/nettest", &tst) == 0) {
		st = run("/bin/nettest", NULL, NULL, -1);
		report("net.suite", st == 0, "see NTEST lines");
	}
	printf("usertest: %d failure(s)\n", failures);
	return failures;
}
