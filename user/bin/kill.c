/* kill PID... */
#include "fu.h"
int main(int argc, char **argv)
{
	int rc = 0;
	for (int i = 1; i < argc; i++) {
		int r = kill(atoi(argv[i]));
		if (r < 0) {
			dprintf(2, "kill: %s: %s\n", argv[i], strerror(r));
			rc = 1;
		}
	}
	return rc;
}
