/* desktop — starts the Fuhrer compositor and the default session apps. */
#include "fu.h"

static void launch(const char *path)
{
	const char *argv[] = { path, NULL };
	spawn(path, argv, NULL);
}

int main(void)
{
	int r = (int)sys2(SYS_DESKTOP_CTL, 1, 0);
	if (r < 0) {
		dprintf(2, "desktop: no framebuffer / compositor unavailable (%s)\n", strerror(r));
		return 1;
	}
	printf("desktop: compositor started; launching Control Center and Terminal\n");
	launch("/bin/control");
	sleep_ms(300);
	launch("/bin/term");
	/* The session lives as long as the machine; windows are managed by the
	 * kernel compositor, apps are independent processes. */
	for (;;)
		sleep_ms(60000);
}
