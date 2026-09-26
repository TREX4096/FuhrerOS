/* top - live view of tasks, classes and the adaptive policy (q to quit) */
#include "fu.h"
int main(int argc, char **argv)
{
	int iters = argc > 1 ? atoi(argv[1]) : 1000000;
	static char buf[32768];
	ioctl(0, TTY_SETMODE, 0);
	for (int i = 0; i < iters; i++) {
		struct fu_sysinfo a, b;
		sysinfo(&a);
		int fds[1] = { 0 };
		int ready = poll_readable(fds, 1, 1000);
		sysinfo(&b);
		if (ready > 0) {
			char c;
			read(0, &c, 1);
			if (c == 'q')
				break;
		}
		uint64_t busy = b.busy_ns - a.busy_ns, idle = b.idle_ns - a.idle_ns;
		int cpu = (int)((busy + idle) ? busy * 100 / (busy + idle) : 0);
		printf("\033[2J\033[H\033[1;36mFuhrerOS top\033[0m  up %lus  cpu %d%%  mem %lu/%lu MiB  policy %s  workload %s (%u%%)  q=quit\n\n",
		       b.uptime_ns / 1000000000UL, cpu, (b.mem_total_kb - b.mem_free_kb) / 1024,
		       b.mem_total_kb / 1024, b.policy, b.system_class, b.class_confidence);
		read_file("/proc/tasks", buf, sizeof(buf));
		printf("%s", buf);
		flush();
	}
	ioctl(0, TTY_SETMODE, TTY_ECHO | TTY_CANON);
	return 0;
}
