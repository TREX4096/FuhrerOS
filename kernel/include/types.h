/* FuhrerOS — freestanding base types. No host headers except the
 * compiler-provided freestanding ones (stdint/stddef/stdbool/stdarg). */
#ifndef FUHRER_TYPES_H
#define FUHRER_TYPES_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;
typedef int64_t ssize_t;
typedef int pid_t;

#define PAGE_SIZE 4096UL
#define PAGE_SHIFT 12
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((__typeof__(x))(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((__typeof__(x))(a) - 1))
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))

#define PACKED __attribute__((packed))
#define NORETURN __attribute__((noreturn))
#define UNUSED __attribute__((unused))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* Kernel error codes (negated when returned). */
enum {
	E_OK = 0,
	E_PERM = 1,
	E_NOENT = 2,
	E_SRCH = 3,
	E_INTR = 4,
	E_IO = 5,
	E_NOEXEC = 8,
	E_BADF = 9,
	E_CHILD = 10,
	E_AGAIN = 11,
	E_NOMEM = 12,
	E_FAULT = 14,
	E_BUSY = 16,
	E_EXIST = 17,
	E_NOTDIR = 20,
	E_ISDIR = 21,
	E_INVAL = 22,
	E_MFILE = 24,
	E_NOSPC = 28,
	E_PIPE = 32,
	E_RANGE = 34,
	E_NAMETOOLONG = 36,
	E_NOSYS = 38,
	E_NOTEMPTY = 39,
	E_NOTSOCK = 88,
	E_ADDRINUSE = 98,
	E_NETUNREACH = 101,
	E_CONNRESET = 104,
	E_NOTCONN = 107,
	E_TIMEDOUT = 110,
	E_CONNREFUSED = 111,
};

#endif
