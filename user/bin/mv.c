/* mv SRC DST — rename (falls back to copy + delete across filesystems) */
#include "fu.h"
int main(int argc, char **argv)
{
	if (argc != 3) {
		dprintf(2, "usage: mv SRC DST\n");
		return 2;
	}
	int r = rename(argv[1], argv[2]);
	if (r == -22) { /* cross-filesystem */
		const char *cp_argv[] = { "/bin/cp", argv[1], argv[2], NULL };
		int st = 1;
		pid_t p = spawn("/bin/cp", cp_argv, NULL);
		if (p > 0)
			wait(p, &st);
		if (st == 0)
			r = unlink(argv[1]);
		else
			r = -5;
	}
	if (r < 0) {
		dprintf(2, "mv: %s: %s\n", argv[1], strerror(r));
		return 1;
	}
	return 0;
}
