/* mkdir DIR... */
#include "fu.h"
int main(int argc, char **argv)
{
	int rc = 0;
	for (int i = 1; i < argc; i++) {
		int r = mkdir(argv[i]);
		if (r < 0) {
			dprintf(2, "mkdir: %s: %s\n", argv[i], strerror(r));
			rc = 1;
		}
	}
	return rc;
}
