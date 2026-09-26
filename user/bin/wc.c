/* wc [FILE] — lines, words, bytes */
#include "fu.h"
int main(int argc, char **argv)
{
	int fd = argc > 1 ? open(argv[1], O_RDONLY) : 0;
	if (fd < 0) {
		dprintf(2, "wc: %s\n", strerror(fd));
		return 1;
	}
	char buf[4096];
	ssize_t n;
	unsigned long l = 0, w = 0, b = 0;
	bool inw = false;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		for (ssize_t i = 0; i < n; i++) {
			b++;
			if (buf[i] == '\n')
				l++;
			if (isspace(buf[i]))
				inw = false;
			else if (!inw) {
				inw = true;
				w++;
			}
		}
	printf("%7lu %7lu %7lu %s\n", l, w, b, argc > 1 ? argv[1] : "");
	return 0;
}
