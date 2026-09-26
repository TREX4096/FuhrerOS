/* FuhrerOS native ABI, shared by the kernel and user space.
 *
 * Calling convention: `syscall` with the number in rax, arguments in
 * rdi, rsi, rdx, r10, r8, r9; result in rax (negative = -errno). */
#ifndef FUHRER_UAPI_H
#define FUHRER_UAPI_H

#include <stdint.h>

enum {
	SYS_EXIT = 0,
	SYS_WRITE = 1,
	SYS_READ = 2,
	SYS_OPEN = 3,
	SYS_CLOSE = 4,
	SYS_YIELD = 5,
	SYS_SLEEP = 6,		/* milliseconds */
	SYS_MMAP = 7,		/* anonymous memory: (hint, len, prot) */
	SYS_MUNMAP = 8,
	SYS_SPAWN = 9,		/* (path, argv, stdio[3] fds or -1) */
	SYS_WAIT = 10,		/* (pid, &status) */
	SYS_GETPID = 11,
	SYS_TIME = 12,		/* (&struct fu_time) */
	SYS_STAT = 13,		/* (path, &stat) */
	SYS_FSTAT = 14,
	SYS_READDIR = 15,	/* (fd, index, &dirent) -> 1 entry, 0 end */
	SYS_MKDIR = 16,
	SYS_UNLINK = 17,
	SYS_RENAME = 18,
	SYS_CHDIR = 19,
	SYS_GETCWD = 20,
	SYS_PIPE = 21,		/* (int fds[2]) */
	SYS_DUP2 = 22,
	SYS_KILL = 23,
	SYS_SYSINFO = 24,	/* (&struct fu_sysinfo) */
	SYS_SCHED_CTL = 25,	/* (op, arg, buf) */
	SYS_SEEK = 26,		/* (fd, off, whence) */
	SYS_IOCTL = 27,
	SYS_BRK = 28,
	SYS_THREAD_CREATE = 29,	/* (entry, stack_top, arg) */
	SYS_THREAD_EXIT = 30,
	SYS_GETPPID = 31,
	SYS_TRUNCATE = 32,
	SYS_SYNC = 33,
	SYS_POLL = 34,		/* (fds[], n, timeout_ms) */
	SYS_PORT_CREATE = 35,
	SYS_PORT_LOOKUP = 36,
	SYS_PORT_SEND = 37,
	SYS_PORT_RECV = 38,	/* (id, buf, max, &sender, timeout_ms) */
	SYS_SOCKET = 40,
	SYS_CONNECT = 41,
	SYS_BIND = 42,
	SYS_LISTEN = 43,
	SYS_ACCEPT = 44,
	SYS_SEND = 45,
	SYS_RECV = 46,
	SYS_SENDTO = 47,
	SYS_RECVFROM = 48,
	SYS_NETINFO = 49,	/* (&struct fu_netinfo) */
	SYS_WIN_CREATE = 50,	/* (w, h, title, flags) -> id; surface via SYS_WIN_SURFACE */
	SYS_WIN_SURFACE = 51,	/* (id) -> user address of the pixel buffer */
	SYS_WIN_PRESENT = 52,	/* (id) */
	SYS_WIN_EVENT = 53,	/* (id or -1, &ev, timeout_ms) */
	SYS_WIN_CLOSE = 54,
	SYS_WIN_SET_TITLE = 55,
	SYS_WIN_INFO = 56,	/* (&struct fu_screen) */
	SYS_WIN_RESIZE = 57,	/* (id, w, h) */
	SYS_DESKTOP_CTL = 58,	/* (op, arg) */
	SYS_BLKSTAT = 59,	/* (&struct fu_blkstat) */
	SYS_CACHE_CTL = 60,	/* (op, arg) */
	SYS_MAX = 64,
};

/* open flags */
#define O_RDONLY 0x0
#define O_WRONLY 0x1
#define O_RDWR 0x2
#define O_ACCMODE 0x3
#define O_CREAT 0x40
#define O_TRUNC 0x200
#define O_APPEND 0x400
#define O_NONBLOCK 0x800
#define O_DIRECTORY 0x10000

/* vnode types */
#define VT_FILE 1
#define VT_DIR 2
#define VT_CHAR 3
#define VT_PIPE 4
#define VT_SOCK 5
#define VT_BLOCK 6

#define NAME_MAX 58
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct dirent {
	uint32_t ino;
	uint8_t type;
	char name[NAME_MAX + 1];
};

struct stat {
	uint32_t ino;
	uint32_t type;
	uint64_t size;
	uint32_t nlink;
	uint32_t blocks;
	uint64_t mtime;
	uint32_t dev;
	uint32_t mode;
};

struct fu_time {
	uint64_t uptime_ns;
	int64_t unix_seconds;
};

struct fu_sysinfo {
	uint64_t mem_total_kb, mem_free_kb;
	uint64_t uptime_ns;
	uint64_t busy_ns, idle_ns;
	uint64_t context_switches;
	uint32_t tasks, procs, runnable, cpus;
	char policy[24];
	char system_class[16];
	uint32_t class_confidence;
	char cpu_brand[48];
	uint64_t tsc_hz;
};

/* SYS_SCHED_CTL operations */
enum {
	SCHED_SET_POLICY = 1,	/* buf = policy name */
	SCHED_GET_POLICY = 2,	/* buf receives name */
	SCHED_SET_PRIORITY = 3,	/* arg = (pid << 8) | prio */
	SCHED_SET_WINDOW = 4,	/* arg = profiler window ms */
	SCHED_SET_HYSTERESIS = 5,
	SCHED_PROFILER = 6,	/* arg = 0 off / 1 on */
	SCHED_QUANTUM_SCALE = 7,/* arg = percent */
	SCHED_TASK_CLASS = 8,	/* arg = tid; returns class */
	SCHED_RESET_STATS = 9,
	SCHED_LAST_DISPATCH = 10,/* returns calling thread's last wake->run latency, ns */
};

/* tty ioctls */
#define TTY_SETMODE 0x5401
#define TTY_GETMODE 0x5402
#define TTY_SETFG 0x5403
#define TTY_GETSIZE 0x5404
#define TTY_ECHO 0x1
#define TTY_CANON 0x2

/* mmap prot */
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

/* sockets */
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_ICMP 3	/* echo request/reply datagrams (ping) */
struct sockaddr_in {
	uint16_t family;
	uint16_t port;		/* network byte order */
	uint32_t addr;		/* network byte order */
	uint8_t zero[8];
};

struct fu_netinfo {
	uint8_t mac[6];
	uint8_t up;
	uint8_t pad;
	uint32_t ip, gateway, netmask, dns;	/* network byte order */
	uint64_t rx_packets, tx_packets, rx_bytes, tx_bytes, rx_dropped;
	uint32_t tcp_active, udp_active, arp_entries;
};

/* windows / desktop */
struct fu_screen {
	uint32_t width, height;
	uint32_t desktop_running;
};

enum { WEV_NONE, WEV_KEY, WEV_MOUSE_MOVE, WEV_MOUSE_BUTTON, WEV_SCROLL, WEV_CLOSE, WEV_RESIZE,
       WEV_FOCUS, WEV_TIMER };
struct fu_wevent {
	uint32_t type;
	int32_t win;
	int32_t x, y;		/* window-relative pointer position */
	uint32_t key;		/* keycode */
	uint32_t ch;		/* character, if any */
	int32_t value;		/* press/release, button number, scroll delta */
	uint32_t mods;
};

struct fu_blkstat {
	uint64_t reads, writes, read_bytes, write_bytes;
	uint64_t cache_hits, cache_misses, readahead_blocks, readahead_hits;
	uint64_t evictions;
	uint32_t cache_blocks, cache_capacity;
	char cache_policy[24];
	char io_class[16];
};

/* SYS_CACHE_CTL */
enum { CACHE_SET_POLICY = 1, CACHE_SET_CAPACITY = 2, CACHE_FLUSH = 3, CACHE_RESET_STATS = 4,
       CACHE_DROP = 5 };

#endif
