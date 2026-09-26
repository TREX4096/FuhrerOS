/* uname */
#include "fu.h"
int main(void)
{
	char buf[256];
	if (read_file("/proc/version", buf, sizeof(buf)) > 0)
		printf("%s", buf);
	return 0;
}
