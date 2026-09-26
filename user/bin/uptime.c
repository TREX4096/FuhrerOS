/* uptime */
#include "fu.h"
int main(void)
{
	uint64_t s = uptime_ns() / 1000000000UL;
	struct fu_sysinfo si;
	sysinfo(&si);
	printf("up %lu:%02lu:%02lu, %u processes, %u tasks, policy %s\n", s / 3600, (s / 60) % 60, s % 60,
	       si.procs, si.tasks, si.policy);
	return 0;
}
