/* df - mounted filesystems */
#include "fu.h"
int main(void)
{
	static char buf[4096];
	read_file("/proc/mounts", buf, sizeof(buf));
	printf("%s", buf);
	return 0;
}
