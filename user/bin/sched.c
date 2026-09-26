/* sched — inspect and control the FuhrerOS scheduler.
 *   sched                     show policy, stats and recent adaptations
 *   sched policy NAME         round_robin | priority | low_latency | adaptive
 *   sched prio PID N          set a process's base priority (0 high .. 31 low)
 *   sched window MS           profiler window
 *   sched hysteresis N        windows needed before a class change
 *   sched profiler on|off
 *   sched quantum PCT         scale every policy's quanta (ablations) */
#include "fu.h"
int main(int argc, char **argv)
{
	int r = 0;
	if (argc >= 3 && !strcmp(argv[1], "policy"))
		r = sched_ctl(SCHED_SET_POLICY, 0, argv[2]);
	else if (argc >= 4 && !strcmp(argv[1], "prio"))
		r = sched_ctl(SCHED_SET_PRIORITY, (atoi(argv[2]) << 8) | (atoi(argv[3]) & 0xFF), NULL);
	else if (argc >= 3 && !strcmp(argv[1], "window"))
		r = sched_ctl(SCHED_SET_WINDOW, atoi(argv[2]), NULL);
	else if (argc >= 3 && !strcmp(argv[1], "hysteresis"))
		r = sched_ctl(SCHED_SET_HYSTERESIS, atoi(argv[2]), NULL);
	else if (argc >= 3 && !strcmp(argv[1], "profiler"))
		r = sched_ctl(SCHED_PROFILER, !strcmp(argv[2], "on"), NULL);
	else if (argc >= 3 && !strcmp(argv[1], "quantum"))
		r = sched_ctl(SCHED_QUANTUM_SCALE, atoi(argv[2]), NULL);
	else if (argc > 1) {
		dprintf(2, "usage: sched [policy NAME | prio PID N | window MS | hysteresis N | profiler on|off | quantum PCT]\n");
		return 2;
	}
	if (r < 0) {
		dprintf(2, "sched: %s\n", strerror(r));
		return 1;
	}
	static char buf[16384];
	read_file("/proc/sched", buf, sizeof(buf));
	printf("%s", buf);
	read_file("/proc/adapt", buf, sizeof(buf));
	printf("---\n%s", buf);
	return 0;
}
