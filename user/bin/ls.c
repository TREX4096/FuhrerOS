/* ls [-l] [PATH...] */
#include "fu.h"
static const char *tname(int t)
{
	switch (t) {
	case VT_DIR: return "d";
	case VT_CHAR: return "c";
	case VT_BLOCK: return "b";
	case VT_PIPE: return "p";
	case VT_SOCK: return "s";
	}
	return "-";
}
static int list(const char *path, bool lng)
{
	struct stat st;
	int r = stat(path, &st);
	if (r < 0) {
		dprintf(2, "ls: %s: %s\n", path, strerror(r));
		return 1;
	}
	if (st.type != VT_DIR) {
		if (lng)
			printf("%s %8lu %s\n", tname((int)st.type), st.size, path);
		else
			printf("%s\n", path);
		return 0;
	}
	int fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		dprintf(2, "ls: %s: %s\n", path, strerror(fd));
		return 1;
	}
	struct dirent de;
	int col = 0;
	for (long i = 0; readdir(fd, i, &de) > 0; i++) {
		if (lng) {
			char full[320];
			snprintf(full, sizeof(full), "%s/%s", strcmp(path, "/") ? path : "", de.name);
			struct stat s2;
			if (stat(full, &s2) < 0)
				memset(&s2, 0, sizeof(s2));
			printf("%s %8lu  %s%s\033[0m%s\n", tname(de.type), s2.size,
			       de.type == VT_DIR ? "\033[1;34m" : "", de.name, de.type == VT_DIR ? "/" : "");
		} else {
			printf("%s%-18s\033[0m", de.type == VT_DIR ? "\033[1;34m" : "", de.name);
			if (++col % 4 == 0)
				putchar('\n');
		}
	}
	if (!lng && col % 4)
		putchar('\n');
	close(fd);
	return 0;
}
int main(int argc, char **argv)
{
	bool lng = false;
	int first = 1, rc = 0;
	if (argc > 1 && !strcmp(argv[1], "-l")) {
		lng = true;
		first = 2;
	}
	if (first >= argc)
		return list(".", lng);
	for (int i = first; i < argc; i++) {
		if (argc - first > 1)
			printf("%s:\n", argv[i]);
		rc |= list(argv[i], lng);
	}
	return rc;
}
