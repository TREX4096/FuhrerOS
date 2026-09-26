/* poweroff - sync filesystems and power off the machine */
#include "fu.h"
int main(void)
{
	sync();
	sys2(SYS_DESKTOP_CTL, 99, 0);
	return 0;
}
