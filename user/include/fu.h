/* libfu — the FuhrerOS user-space runtime (NEW_EXPLANATION §19 "libfuhrer").
 * A small, freestanding C library over the native syscall ABI. It is not
 * POSIX, but names follow familiar conventions where the meaning matches. */
#ifndef FU_H
#define FU_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../kernel/include/uapi/fuhrer.h"

typedef int64_t ssize_t;
typedef int pid_t;

/* ---- raw syscalls ---- */
long syscall6(long nr, long a, long b, long c, long d, long e);
#define sys0(n) syscall6((n), 0, 0, 0, 0, 0)
#define sys1(n, a) syscall6((n), (long)(a), 0, 0, 0, 0)
#define sys2(n, a, b) syscall6((n), (long)(a), (long)(b), 0, 0, 0)
#define sys3(n, a, b, c) syscall6((n), (long)(a), (long)(b), (long)(c), 0, 0)
#define sys4(n, a, b, c, d) syscall6((n), (long)(a), (long)(b), (long)(c), (long)(d), 0)
#define sys5(n, a, b, c, d, e) syscall6((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e))

/* ---- process / files ---- */
__attribute__((noreturn)) void exit(int code);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int open(const char *path, int flags);
int close(int fd);
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int readdir(int fd, long index, struct dirent *de);
int mkdir(const char *path);
int unlink(const char *path);
int rename(const char *from, const char *to);
int chdir(const char *path);
char *getcwd(char *buf, size_t n);
int pipe(int fds[2]);
int dup2(int from, int to);
long lseek(int fd, long off, int whence);
int ioctl(int fd, unsigned long req, unsigned long arg);
int truncate(const char *path, uint64_t len);
void sync(void);
pid_t spawn(const char *path, const char *const *argv, const int stdio[3]);
pid_t spawnv(const char *path, char *const argv[]); /* inherit stdio */
pid_t wait(pid_t pid, int *status);
pid_t getpid(void);
pid_t getppid(void);
int kill(pid_t pid);
void yield(void);
void sleep_ms(uint64_t ms);
uint64_t uptime_ns(void);
int64_t unix_time(void);
int sysinfo(struct fu_sysinfo *si);
int sched_ctl(int op, long arg, char *buf);
int thread_create(void (*entry)(void *), void *arg, size_t stack_size);
int poll_readable(const int *fds, int n, uint64_t timeout_ms);
int port_create(const char *name);
int port_lookup(const char *name);
int port_send(int port, const void *msg, uint32_t len);
int port_recv(int port, void *buf, uint32_t max, pid_t *sender, uint64_t timeout_ms);
void *mmap_anon(size_t len);

/* ---- memory ---- */
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void free(void *p);

/* ---- strings ---- */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
size_t strlcpy(char *d, const char *s, size_t n);
size_t strlcat(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *strdup(const char *s);
char *strtok_r(char *s, const char *delim, char **save);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int atoi(const char *s);
int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int toupper(int c);
int tolower(int c);

/* ---- formatted I/O (fd based) ---- */
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int snprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int dprintf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int puts(const char *s);
int putchar(int c);
void flush(void);			/* stdout is line buffered */
ssize_t readline(int fd, char *buf, size_t n);	/* reads through '\n' */
const char *strerror(int err);
void perror_code(const char *what, int err);

/* ---- networking ---- */
int socket(int domain, int type);
int connect(int fd, uint32_t ip_be, uint16_t port);
int bind(int fd, uint16_t port);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr_in *from);
ssize_t send(int fd, const void *b, size_t n);
ssize_t recv(int fd, void *b, size_t n);
ssize_t sendto(int fd, const void *b, size_t n, uint32_t ip_be, uint16_t port);
ssize_t recvfrom(int fd, void *b, size_t n, struct sockaddr_in *from, uint64_t timeout_ms);
int netinfo(struct fu_netinfo *ni);
/* Resolve a host name (or dotted quad) to an IPv4 address in network order. */
int resolve(const char *host, uint32_t *ip_be);
int parse_ip(const char *s, uint32_t *ip_be);
void format_ip(uint32_t ip_be, char *out);

/* ---- misc ---- */
void qsort_u64(uint64_t *a, long n);
uint32_t rand32(void);
void srand32(uint32_t seed);
static inline uint16_t htons(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v)
{
	return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }
/* read a whole small file (e.g. /proc/...) into buf; returns length or -errno */
ssize_t read_file(const char *path, char *buf, size_t n);
/* Find "key: value" or "key=value" in text; copies the value (to end of line). */
bool text_value(const char *text, const char *key, char *out, size_t n);
long text_num(const char *text, const char *key);

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#endif
