/* dmesg - kernel log */
#include "fu.h"
int main(void)
{
	static char buf[65536];
	int fd = open("/proc/kmsg", O_RDONLY);
	if (fd < 0)
		return 1;
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		write(1, buf, (size_t)n);
	return 0;
}
