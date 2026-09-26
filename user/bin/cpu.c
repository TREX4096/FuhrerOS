/* cpu - processor and scheduler summary */
#include "fu.h"
int main(void)
{
	static char buf[4096];
	read_file("/proc/cpuinfo", buf, sizeof(buf));
	printf("%s", buf);
	struct fu_sysinfo a, b;
	sysinfo(&a);
	sleep_ms(500);
	sysinfo(&b);
	uint64_t busy = b.busy_ns - a.busy_ns, idle = b.idle_ns - a.idle_ns;
	printf("utilisation (500 ms): %lu%%\n", (busy + idle) ? busy * 100 / (busy + idle) : 0);
	printf("scheduler: %s, system workload: %s (confidence %u%%)\n", b.policy, b.system_class,
	       b.class_confidence);
	printf("context switches/s: %lu\n", (b.context_switches - a.context_switches) * 2);
	return 0;
}
