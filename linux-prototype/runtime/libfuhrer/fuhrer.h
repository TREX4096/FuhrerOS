/*
 * libfuhrer — runtime library for Fuhrer-aware applications.
 *
 * Unmodified Linux programs benefit from FuhrerOS only through the kernel
 * tunables fuhrerd switches. Programs that link libfuhrer additionally get an
 * I/O path that follows the active policy:
 *
 *   NORMAL       one pread/pwrite syscall per request
 *   BATCHED      io_uring, whole batch submitted with a single io_uring_enter
 *   SPECIALIZED  io_uring on an O_DIRECT descriptor (page cache bypass) for
 *                aligned requests; unaligned requests fall back to BATCHED
 */
#ifndef LIBFUHRER_H
#define LIBFUHRER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum fu_backend {
	FU_BACKEND_NORMAL = 0,		/* same numbering as enum fu_policy_id */
	FU_BACKEND_BATCHED = 1,
	FU_BACKEND_SPECIALIZED = 2,
	FU_BACKEND_AUTO = 3,		/* follow fuhrerd's published policy */
};

enum fu_op { FU_OP_READ = 0, FU_OP_WRITE = 1 };

struct fu_req {
	int op;			/* enum fu_op */
	void *buf;
	size_t len;
	off_t off;
	ssize_t res;		/* bytes or -errno, filled on completion */
	double lat_us;		/* submit -> completion */
};

struct fu_io_stats {
	uint64_t ops[3];	/* per backend actually used */
	uint64_t batches;
	uint64_t backend_switches;
	uint64_t direct_fallbacks;	/* SPECIALIZED requests that were unaligned */
};

struct fu_io;

/* Current policy published by fuhrerd, or -1 when the daemon is absent.
 * cls/mode may be NULL. Lock-free; safe to call per request. */
int fu_current_policy(int *cls, int *mode);
const char *fu_backend_name(int backend);
int fu_backend_from_name(const char *s);

/* depth: maximum requests per batch (io_uring SQ size). */
struct fu_io *fu_io_open(const char *path, int flags, mode_t mode, int backend, int depth);
int fu_io_close(struct fu_io *io);
/* Execute n requests (n <= depth). Returns #successful, or -errno on setup
 * failure. Per-request results and latencies are stored in reqs. */
int fu_io_run(struct fu_io *io, struct fu_req *reqs, int n);
int fu_io_fsync(struct fu_io *io);
int fu_io_backend_last(const struct fu_io *io);
void fu_io_get_stats(const struct fu_io *io, struct fu_io_stats *st);
/* Allocate a buffer suitable for O_DIRECT (4 KiB aligned). */
void *fu_io_alloc(size_t len);

#ifdef __cplusplus
}
#endif
#endif
