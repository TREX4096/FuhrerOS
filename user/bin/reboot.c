/* reboot - sync filesystems and reset the machine */
#include "fu.h"
int main(void)
{
	sync();
	sys2(SYS_DESKTOP_CTL, DESK_REBOOT, 0);
	return 0;
}
