/* poweroff - sync filesystems and power off (ACPI S5) the machine
 *   poweroff        power off
 *   poweroff -r     reboot (same as /bin/reboot) */
#include "fu.h"
int main(int argc, char **argv)
{
	bool reboot = argc > 1 && !strcmp(argv[1], "-r");
	sync();
	sys2(SYS_DESKTOP_CTL, reboot ? DESK_REBOOT : DESK_POWEROFF, 0);
	return 0;
}
