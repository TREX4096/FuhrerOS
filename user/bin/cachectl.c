/* cachectl — buffer cache control.
 *   cachectl                     statistics
 *   cachectl policy NAME         lru | fifo | clock | readahead | adaptive
 *   cachectl capacity BLOCKS     resize (4 KiB blocks)
 *   cachectl drop | flush | reset */
#include "fu.h"

int main(int argc, char **argv)
{
	int r = 0;
	if (argc >= 3 && !strcmp(argv[1], "policy"))
		r = (int)sys3(SYS_CACHE_CTL, CACHE_SET_POLICY, 0, argv[2]);
	else if (argc >= 3 && !strcmp(argv[1], "capacity"))
		r = (int)sys3(SYS_CACHE_CTL, CACHE_SET_CAPACITY, atoi(argv[2]), 0);
	else if (argc >= 2 && !strcmp(argv[1], "drop"))
		r = (int)sys3(SYS_CACHE_CTL, CACHE_DROP, 0, 0);
	else if (argc >= 2 && !strcmp(argv[1], "flush"))
		r = (int)sys3(SYS_CACHE_CTL, CACHE_FLUSH, 0, 0);
	else if (argc >= 2 && !strcmp(argv[1], "reset"))
		r = (int)sys3(SYS_CACHE_CTL, CACHE_RESET_STATS, 0, 0);
	else if (argc > 1) {
		dprintf(2, "usage: cachectl [policy NAME | capacity N | drop | flush | reset]\n");
		return 2;
	}
	if (r < 0) {
		dprintf(2, "cachectl: %s\n", strerror(r));
		return 1;
	}
	static char buf[4096];
	read_file("/proc/bcache", buf, sizeof(buf));
	printf("%s", buf);
	return 0;
}
