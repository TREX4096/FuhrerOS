/* libfu: syscall wrappers, heap, buffered stdout, helpers. The string and
 * printf routines are the kernel's own sources compiled again for user mode
 * (kernel/lib/string.c, kernel/lib/printf.c). */
#include "fu.h"

/* ---- syscall wrappers ---- */
__attribute__((noreturn)) void exit(int code)
{
	flush();
	sys1(SYS_EXIT, code);
	for (;;)
		;
}
ssize_t read(int fd, void *b, size_t n) { return sys3(SYS_READ, fd, b, n); }
ssize_t write(int fd, const void *b, size_t n)
{
	if (fd == 1)
		flush();
	return sys3(SYS_WRITE, fd, b, n);
}
int open(const char *p, int f) { return (int)sys2(SYS_OPEN, p, f); }
int close(int fd) { return (int)sys1(SYS_CLOSE, fd); }
int stat(const char *p, struct stat *st) { return (int)sys2(SYS_STAT, p, st); }
int fstat(int fd, struct stat *st) { return (int)sys2(SYS_FSTAT, fd, st); }
int readdir(int fd, long i, struct dirent *de) { return (int)sys3(SYS_READDIR, fd, i, de); }
int mkdir(const char *p) { return (int)sys1(SYS_MKDIR, p); }
int unlink(const char *p) { return (int)sys1(SYS_UNLINK, p); }
int rename(const char *a, const char *b) { return (int)sys2(SYS_RENAME, a, b); }
int chdir(const char *p) { return (int)sys1(SYS_CHDIR, p); }
char *getcwd(char *b, size_t n) { return sys2(SYS_GETCWD, b, n) < 0 ? NULL : b; }
int pipe(int fds[2]) { return (int)sys1(SYS_PIPE, fds); }
int dup2(int a, int b) { return (int)sys2(SYS_DUP2, a, b); }
long lseek(int fd, long off, int wh) { return sys3(SYS_SEEK, fd, off, wh); }
int ioctl(int fd, unsigned long r, unsigned long a) { return (int)sys3(SYS_IOCTL, fd, r, a); }
int truncate(const char *p, uint64_t l) { return (int)sys2(SYS_TRUNCATE, p, l); }
void sync(void) { sys0(SYS_SYNC); }
pid_t spawn(const char *p, const char *const *argv, const int stdio[3])
{
	flush();
	return (pid_t)sys3(SYS_SPAWN, p, argv, stdio);
}
pid_t spawnv(const char *p, char *const argv[]) { return spawn(p, (const char *const *)argv, NULL); }
pid_t wait(pid_t pid, int *st) { return (pid_t)sys2(SYS_WAIT, pid, st); }
pid_t getpid(void) { return (pid_t)sys0(SYS_GETPID); }
pid_t getppid(void) { return (pid_t)sys0(SYS_GETPPID); }
int kill(pid_t pid) { return (int)sys1(SYS_KILL, pid); }
void yield(void) { sys0(SYS_YIELD); }
void sleep_ms(uint64_t ms) { flush(); sys1(SYS_SLEEP, ms); }
uint64_t uptime_ns(void)
{
	struct fu_time t;
	sys1(SYS_TIME, &t);
	return t.uptime_ns;
}
int64_t unix_time(void)
{
	struct fu_time t;
	sys1(SYS_TIME, &t);
	return t.unix_seconds;
}
int sysinfo(struct fu_sysinfo *si) { return (int)sys1(SYS_SYSINFO, si); }
int sched_ctl(int op, long arg, char *buf) { return (int)sys3(SYS_SCHED_CTL, op, arg, buf); }
int poll_readable(const int *fds, int n, uint64_t t) { return (int)sys3(SYS_POLL, fds, n, t); }
int port_create(const char *n) { return (int)sys1(SYS_PORT_CREATE, n); }
int port_lookup(const char *n) { return (int)sys1(SYS_PORT_LOOKUP, n); }
int port_send(int p, const void *m, uint32_t l) { return (int)sys3(SYS_PORT_SEND, p, m, l); }
int port_recv(int p, void *b, uint32_t m, pid_t *s, uint64_t t)
{
	return (int)sys5(SYS_PORT_RECV, p, b, m, s, t);
}
void *mmap_anon(size_t len)
{
	long r = sys3(SYS_MMAP, 0, len, PROT_READ | PROT_WRITE);
	return r < 0 ? NULL : (void *)r;
}

/* Threads get their own mmap'ed stack; the trampoline passes arg in rdi. */
struct thread_start {
	void (*fn)(void *);
	void *arg;
};
static void thread_trampoline(struct thread_start *ts)
{
	ts->fn(ts->arg);
	sys1(SYS_THREAD_EXIT, 0);
}
int thread_create(void (*entry)(void *), void *arg, size_t stack_size)
{
	if (!stack_size)
		stack_size = 64 * 1024;
	uint8_t *stack = mmap_anon(stack_size);
	if (!stack)
		return -12;
	struct thread_start *ts = (struct thread_start *)(stack + stack_size - 64);
	ts->fn = entry;
	ts->arg = arg;
	return (int)sys3(SYS_THREAD_CREATE, thread_trampoline, (uintptr_t)ts - 64, ts);
}

/* ---- heap: size-class free lists over brk, big blocks via mmap ---- */
struct hdr {
	size_t size;	/* usable bytes */
	size_t magic;
};
#define HMAGIC 0xF0F0A5A5C3C3E1E1UL
#define NBINS 16
static struct hdr *bins[NBINS];
static uint8_t *brk_cur, *brk_end;

static int bin_of(size_t n)
{
	int b = 0;
	size_t s = 16;
	while (s < n && b < NBINS - 1) {
		s <<= 1;
		b++;
	}
	return b;
}

static void *morecore(size_t n)
{
	if (!brk_cur) {
		brk_cur = (uint8_t *)sys1(SYS_BRK, 0);
		brk_end = brk_cur;
	}
	if (brk_cur + n > brk_end) {
		size_t grow = (n + 65535) & ~65535UL;
		long r = sys1(SYS_BRK, brk_end + grow);
		if (r < 0 || (uint8_t *)r < brk_end + grow)
			return NULL;
		brk_end += grow;
	}
	void *p = brk_cur;
	brk_cur += n;
	return p;
}

void *malloc(size_t n)
{
	if (!n)
		n = 1;
	n = (n + 15) & ~15UL;
	if (n > (1UL << 20)) {
		struct hdr *h = mmap_anon(n + sizeof(struct hdr));
		if (!h)
			return NULL;
		h->size = n;
		h->magic = HMAGIC;
		return h + 1;
	}
	int b = bin_of(n);
	size_t sz = 16UL << b;
	struct hdr *h = bins[b];
	if (h) {
		bins[b] = *(struct hdr **)(h + 1);
	} else {
		h = morecore(sz + sizeof(struct hdr));
		if (!h)
			return NULL;
		h->size = sz;
	}
	h->magic = HMAGIC;
	return h + 1;
}

void free(void *p)
{
	if (!p)
		return;
	struct hdr *h = (struct hdr *)p - 1;
	if (h->magic != HMAGIC)
		return; /* double free or garbage: ignore rather than corrupt */
	h->magic = 0;
	if (h->size > (1UL << 20))
		return; /* large blocks are not recycled in this simple allocator */
	int b = bin_of(h->size);
	*(struct hdr **)(h + 1) = bins[b];
	bins[b] = h;
}

void *calloc(size_t n, size_t m)
{
	void *p = malloc(n * m);
	if (p)
		memset(p, 0, n * m);
	return p;
}

void *realloc(void *p, size_t n)
{
	if (!p)
		return malloc(n);
	struct hdr *h = (struct hdr *)p - 1;
	if (n <= h->size)
		return p;
	void *q = malloc(n);
	if (q) {
		memcpy(q, p, h->size);
		free(p);
	}
	return q;
}

/* ---- more string helpers ---- */
char *strstr(const char *h, const char *n)
{
	size_t l = strlen(n);
	for (; *h; h++)
		if (!strncmp(h, n, l))
			return (char *)h;
	return l ? NULL : (char *)h;
}
char *strdup(const char *s)
{
	size_t l = strlen(s) + 1;
	char *d = malloc(l);
	if (d)
		memcpy(d, s, l);
	return d;
}
char *strtok_r(char *s, const char *delim, char **save)
{
	if (!s)
		s = *save;
	while (*s && strchr(delim, *s))
		s++;
	if (!*s) {
		*save = s;
		return NULL;
	}
	char *t = s;
	while (*s && !strchr(delim, *s))
		s++;
	if (*s)
		*s++ = 0;
	*save = s;
	return t;
}
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
int isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* ---- stdout buffering ---- */
/* stdout buffer, shared by all threads of a process: guarded by a spinlock
 * (yielding while contended) so concurrent flushes cannot duplicate data. */
static char obuf[1024];
static size_t olen;
static volatile char olock;

static void olock_take(void)
{
	while (__atomic_test_and_set(&olock, __ATOMIC_ACQUIRE))
		sys0(SYS_YIELD);
}
static void olock_drop(void) { __atomic_clear(&olock, __ATOMIC_RELEASE); }

static void flush_locked(void)
{
	if (olen) {
		sys3(SYS_WRITE, 1, obuf, olen);
		olen = 0;
	}
}

void flush(void)
{
	olock_take();
	flush_locked();
	olock_drop();
}

int putchar(int c)
{
	olock_take();
	obuf[olen++] = (char)c;
	if (c == '\n' || olen == sizeof(obuf))
		flush_locked();
	olock_drop();
	return c;
}

static void out_str(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		putchar(s[i]);
}

int printf(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	out_str(buf, (size_t)MIN(n, (int)sizeof(buf) - 1));
	return n;
}

int dprintf(int fd, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (fd == 1) {
		out_str(buf, (size_t)MIN(n, (int)sizeof(buf) - 1));
		return n;
	}
	return (int)write(fd, buf, (size_t)MIN(n, (int)sizeof(buf) - 1));
}

int puts(const char *s)
{
	out_str(s, strlen(s));
	putchar('\n');
	return 0;
}

ssize_t readline(int fd, char *buf, size_t n)
{
	size_t i = 0;
	while (i + 1 < n) {
		char c;
		ssize_t r = read(fd, &c, 1);
		if (r <= 0) {
			if (i == 0)
				return r;
			break;
		}
		buf[i++] = c;
		if (c == '\n')
			break;
	}
	buf[i] = 0;
	return (ssize_t)i;
}

ssize_t read_file(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return fd;
	size_t got = 0;
	while (got + 1 < n) {
		ssize_t r = read(fd, buf + got, n - 1 - got);
		if (r <= 0)
			break;
		got += (size_t)r;
	}
	close(fd);
	buf[got] = 0;
	return (ssize_t)got;
}

const char *strerror(int err)
{
	if (err < 0)
		err = -err;
	switch (err) {
	case 1: return "operation not permitted";
	case 2: return "no such file or directory";
	case 3: return "no such process";
	case 5: return "I/O error";
	case 8: return "not an executable";
	case 9: return "bad file descriptor";
	case 10: return "no child processes";
	case 11: return "try again";
	case 12: return "out of memory";
	case 14: return "bad address";
	case 17: return "file exists";
	case 20: return "not a directory";
	case 21: return "is a directory";
	case 22: return "invalid argument";
	case 24: return "too many open files";
	case 28: return "no space left";
	case 32: return "broken pipe";
	case 36: return "name too long";
	case 38: return "not implemented";
	case 39: return "directory not empty";
	case 101: return "network unreachable";
	case 104: return "connection reset";
	case 110: return "timed out";
	case 111: return "connection refused";
	}
	return "error";
}

void perror_code(const char *what, int err) { dprintf(2, "%s: %s\n", what, strerror(err)); }

static uint32_t rng = 2463534242u;
uint32_t rand32(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}
void srand32(uint32_t s) { rng = s ? s : 1; }

void __libfu_init(void) { srand32((uint32_t)uptime_ns()); }

bool text_value(const char *text, const char *key, char *out, size_t n)
{
	size_t kl = strlen(key);
	for (const char *p = text; p && *p;) {
		if (!strncmp(p, key, kl) && (p[kl] == ':' || p[kl] == '=')) {
			p += kl + 1;
			while (*p == ' ')
				p++;
			size_t i = 0;
			while (p[i] && p[i] != '\n' && i + 1 < n) {
				out[i] = p[i];
				i++;
			}
			out[i] = 0;
			return true;
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	if (n)
		out[0] = 0;
	return false;
}

long text_num(const char *text, const char *key)
{
	char v[32];
	return text_value(text, key, v, sizeof(v)) ? strtol(v, NULL, 10) : 0;
}

/* In-place quicksort (median of three, insertion sort for small ranges). */
void qsort_u64(uint64_t *a, long n)
{
	while (n > 16) {
		uint64_t x = a[0], y = a[n / 2], z = a[n - 1];
		uint64_t piv = x < y ? (y < z ? y : (x < z ? z : x)) : (x < z ? x : (y < z ? z : y));
		long i = 0, j = n - 1;
		for (;;) {
			while (a[i] < piv)
				i++;
			while (a[j] > piv)
				j--;
			if (i >= j)
				break;
			uint64_t t = a[i];
			a[i] = a[j];
			a[j] = t;
			i++;
			j--;
		}
		/* recurse on the smaller part, loop on the larger */
		if (j + 1 < n - j - 1) {
			qsort_u64(a, j + 1);
			a += j + 1;
			n -= j + 1;
		} else {
			qsort_u64(a + j + 1, n - j - 1);
			n = j + 1;
		}
	}
	for (long i = 1; i < n; i++) {
		uint64_t v = a[i];
		long k = i - 1;
		while (k >= 0 && a[k] > v) {
			a[k + 1] = a[k];
			k--;
		}
		a[k + 1] = v;
	}
}
