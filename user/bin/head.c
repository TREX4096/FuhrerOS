/* head [-n N] [FILE] */
#include "fu.h"
int main(int argc, char **argv)
{
	int n = 10, i = 1;
	if (argc > 2 && !strcmp(argv[1], "-n")) {
		n = atoi(argv[2]);
		i = 3;
	}
	int fd = i < argc ? open(argv[i], O_RDONLY) : 0;
	if (fd < 0) {
		dprintf(2, "head: %s\n", strerror(fd));
		return 1;
	}
	char line[1024];
	while (n-- > 0 && readline(fd, line, sizeof(line)) > 0)
		printf("%s", line);
	return 0;
}
