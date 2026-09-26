/* mem — memory usage */
#include "fu.h"
int main(void)
{
	struct fu_sysinfo si;
	sysinfo(&si);
	uint64_t used = si.mem_total_kb - si.mem_free_kb;
	int pct = (int)(used * 100 / (si.mem_total_kb ? si.mem_total_kb : 1));
	printf("Memory: %lu MiB used / %lu MiB total (%d%%)  [", used / 1024, si.mem_total_kb / 1024, pct);
	for (int i = 0; i < 30; i++)
		putchar(i < pct * 30 / 100 ? '#' : '.');
	printf("]\n");
	static char buf[2048];
	if (read_file("/proc/meminfo", buf, sizeof(buf)) > 0)
		printf("%s", buf);
	return 0;
}
