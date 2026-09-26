/* stat PATH */
#include "fu.h"
int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		struct stat st;
		int r = stat(argv[i], &st);
		if (r < 0) {
			dprintf(2, "stat: %s: %s\n", argv[i], strerror(r));
			return 1;
		}
		static const char *t[] = { "?", "file", "directory", "char device", "pipe", "socket", "block device" };
		printf("  File: %s\n  Type: %s\n  Size: %lu  Blocks: %u  Inode: %u  Links: %u  Dev: %u\n  Mtime: %lu\n",
		       argv[i], st.type < 7 ? t[st.type] : "?", st.size, st.blocks, st.ino, st.nlink, st.dev, st.mtime);
	}
	return 0;
}
