/* notify - show a desktop notification:  notify TITLE [TEXT] */
#include "fu.h"
int main(int argc, char **argv)
{
	if (argc < 2) {
		dprintf(2, "usage: notify TITLE [TEXT]\n");
		return 2;
	}
	int r = (int)sys3(SYS_DESKTOP_CTL, DESK_NOTIFY, argv[1], argc > 2 ? argv[2] : "");
	if (r < 0) {
		dprintf(2, "notify: %s\n", strerror(r));
		return 1;
	}
	return 0;
}
