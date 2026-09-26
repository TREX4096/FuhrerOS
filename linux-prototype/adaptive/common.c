#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "fuhrer/common.h"

char fu_sys_root[256] = "";

static void fu_path(char *out, size_t len, const char *path)
{
	snprintf(out, len, "%s%s", fu_sys_root, path);
}

ssize_t fu_read_file_abs(const char *path, char *buf, size_t len)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	size_t off = 0;
	while (off + 1 < len) {
		ssize_t n = read(fd, buf + off, len - 1 - off);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return -1;
		}
		if (n == 0)
			break;
		off += (size_t)n;
	}
	buf[off] = '\0';
	close(fd);
	return (ssize_t)off;
}

ssize_t fu_read_file(const char *path, char *buf, size_t len)
{
	char full[512];
	fu_path(full, sizeof(full), path);
	return fu_read_file_abs(full, buf, len);
}

int fu_write_file(const char *path, const char *value)
{
	char full[512];
	fu_path(full, sizeof(full), path);
	int fd = open(full, O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	size_t n = strlen(value);
	ssize_t w = write(fd, value, n);
	int err = (w == (ssize_t)n) ? 0 : (w < 0 ? -errno : -EIO);
	close(fd);
	return err;
}

int fu_write_atomic(const char *path, const char *data, size_t len)
{
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0)
		return -errno;
	size_t off = 0;
	while (off < len) {
		ssize_t w = write(fd, data + off, len - off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			int err = -errno;
			close(fd);
			unlink(tmp);
			return err;
		}
		off += (size_t)w;
	}
	close(fd);
	if (rename(tmp, path) < 0) {
		int err = -errno;
		unlink(tmp);
		return err;
	}
	return 0;
}

char *fu_rtrim(char *s)
{
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t' ||
			 s[n - 1] == '\r'))
		s[--n] = '\0';
	return s;
}

void fu_log(const char *level, const char *fmt, ...)
{
	char ts[32];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm);
	fprintf(stderr, "%s [%s] ", ts, level);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}
