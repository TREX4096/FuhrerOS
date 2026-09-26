/* ps — tasks with their scheduling class (from /proc/tasks) */
#include "fu.h"
int main(void)
{
	static char buf[32768];
	if (read_file("/proc/tasks", buf, sizeof(buf)) < 0) {
		dprintf(2, "ps: cannot read /proc/tasks\n");
		return 1;
	}
	printf("%s", buf);
	return 0;
}
