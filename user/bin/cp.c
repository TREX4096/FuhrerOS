/* cp SRC DST — copy a file (DST may be a directory) */
#include "fu.h"
int main(int argc, char **argv)
{
	if (argc != 3) {
		dprintf(2, "usage: cp SRC DST\n");
		return 2;
	}
	char dst[320];
	struct stat st;
	if (stat(argv[2], &st) == 0 && st.type == VT_DIR) {
		const char *b = strrchr(argv[1], '/');
		snprintf(dst, sizeof(dst), "%s/%s", argv[2], b ? b + 1 : argv[1]);
	} else {
		strlcpy(dst, argv[2], sizeof(dst));
	}
	int in = open(argv[1], O_RDONLY);
	if (in < 0) {
		dprintf(2, "cp: %s: %s\n", argv[1], strerror(in));
		return 1;
	}
	int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
	if (out < 0) {
		dprintf(2, "cp: %s: %s\n", dst, strerror(out));
		return 1;
	}
	static char buf[16384];
	ssize_t n;
	while ((n = read(in, buf, sizeof(buf))) > 0)
		if (write(out, buf, (size_t)n) != n) {
			dprintf(2, "cp: write failed\n");
			return 1;
		}
	close(in);
	close(out);
	return 0;
}
