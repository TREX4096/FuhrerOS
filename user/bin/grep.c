/* grep PATTERN [FILE] — fixed-string search */
#include "fu.h"
int main(int argc, char **argv)
{
	if (argc < 2) {
		dprintf(2, "usage: grep PATTERN [FILE]\n");
		return 2;
	}
	int fd = argc > 2 ? open(argv[2], O_RDONLY) : 0;
	if (fd < 0) {
		dprintf(2, "grep: %s\n", strerror(fd));
		return 2;
	}
	char line[1024];
	int found = 0;
	while (readline(fd, line, sizeof(line)) > 0)
		if (strstr(line, argv[1])) {
			printf("%s", line);
			found = 1;
		}
	return found ? 0 : 1;
}
