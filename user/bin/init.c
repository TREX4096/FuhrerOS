/* init - first user process (pid 1). Brings up the session:
 *   kernel cmdline "desktop"  -> the graphical desktop (/bin/desktop)
 *   kernel cmdline "usertest" -> the user-space test suite, then power off
 *   otherwise                 -> the shell on the console, respawned on exit */
#include "fu.h"

static bool cmdline_has(const char *w)
{
	char buf[256];
	if (read_file("/proc/cmdline", buf, sizeof(buf)) <= 0)
		return false;
	char *save, *t = strtok_r(buf, " \n", &save);
	for (; t; t = strtok_r(NULL, " \n", &save))
		if (!strcmp(t, w))
			return true;
	return false;
}

static int run(const char *path, const char *arg)
{
	const char *argv[] = { path, arg, NULL };
	pid_t pid = spawn(path, argv, NULL);
	if (pid < 0) {
		dprintf(2, "init: cannot start %s: %s\n", path, strerror(pid));
		return pid;
	}
	int st = 0;
	wait(pid, &st);
	return st;
}

int main(int argc, char **argv)
{
	printf("\033[1;36mFuhrerOS init\033[0m (pid %d): user space is running outside kernel privilege\n",
	       getpid());
	/* Make sure the standard directories exist on a writable root. */
	mkdir("/home");
	mkdir("/tmp");
	if (cmdline_has("usertest")) {
		int st = run("/bin/usertest", NULL);
		printf("init: usertest finished with status %d\n", st);
		sys1(SYS_DESKTOP_CTL, 0x7E57 + (st ? 1 : 0)); /* ask the kernel to end the test run */
		return st;
	}
	/* bench=NAME: run /etc/bench/NAME.sh unattended, then power off */
	char cl[256];
	if (read_file("/proc/cmdline", cl, sizeof(cl)) > 0) {
		char *b = strstr(cl, "bench=");
		if (b) {
			char name[32];
			int i = 0;
			for (b += 6; *b && *b != ' ' && *b != '\n' && i < 31; b++)
				name[i++] = *b;
			name[i] = 0;
			char script[64];
			snprintf(script, sizeof(script), "/etc/bench/%s.sh", name);
			printf("init: running benchmark suite %s\n", script);
			int st = run("/bin/sh", script);
			printf("init: benchmark suite finished (%d)\n", st);
			sync();
			sys2(SYS_DESKTOP_CTL, 99, 0);
		}
	}
	if (cmdline_has("desktop")) {
		for (;;) {
			int st = run("/bin/desktop", NULL);
			printf("init: desktop exited (%d); falling back to the shell\n", st);
			break;
		}
	}
	for (;;) {
		run("/bin/sh", NULL);
		printf("init: shell exited; starting a new one\n");
	}
}
