#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "fuhrer.h"
#include "fuhrer/shm.h"

#define DIRECT_ALIGN 4096

struct fu_io {
	int fd;			/* buffered descriptor */
	int dfd;		/* O_DIRECT descriptor, -1 until first needed */
	int flags;
	char *path;
	int backend;		/* requested (may be AUTO) */
	int last;		/* backend used for the last batch */
	int depth;
	int ring_ok;
	struct io_uring ring;
	struct fu_io_stats st;
};

static double now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

/* ---- shared policy record ---- */

static const struct fu_shared_state *shm_map(void)
{
	static const struct fu_shared_state *cached;
	static double last_try;
	if (cached)
		return cached;
	/* Retry at most once a second if the daemon is not up yet. */
	double t = now_us();
	if (last_try && t - last_try < 1e6)
		return NULL;
	last_try = t;
	const char *path = getenv("FUHRER_SHM");
	int fd = open(path ? path : FU_SHM_PATH, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	void *m = mmap(NULL, sizeof(struct fu_shared_state), PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (m == MAP_FAILED)
		return NULL;
	const struct fu_shared_state *s = m;
	if (s->magic != FU_SHM_MAGIC || s->version != FU_SHM_VERSION) {
		munmap(m, sizeof(struct fu_shared_state));
		return NULL;
	}
	cached = s;
	return s;
}

int fu_current_policy(int *cls, int *mode)
{
	const struct fu_shared_state *s = shm_map();
	if (!s)
		return -1;
	for (int tries = 0; tries < 100; tries++) {
		uint64_t a = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
		if (a & 1)
			continue;
		int p = s->policy, c = s->cls, m = s->mode;
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		if (__atomic_load_n(&s->seq, __ATOMIC_ACQUIRE) == a) {
			if (cls)
				*cls = c;
			if (mode)
				*mode = m;
			return p;
		}
	}
	return -1;
}

static const char *backend_names[] = { "normal", "batched", "specialized", "auto" };

const char *fu_backend_name(int b)
{
	return (b >= 0 && b <= FU_BACKEND_AUTO) ? backend_names[b] : "unknown";
}

int fu_backend_from_name(const char *s)
{
	for (int i = 0; i <= FU_BACKEND_AUTO; i++)
		if (strcasecmp(s, backend_names[i]) == 0)
			return i;
	if (!strcasecmp(s, "fast") || !strcasecmp(s, "spec"))
		return FU_BACKEND_SPECIALIZED;
	if (!strcasecmp(s, "batch"))
		return FU_BACKEND_BATCHED;
	return -1;
}

/* ---- setup ---- */

void *fu_io_alloc(size_t len)
{
	void *p = NULL;
	size_t rounded = (len + DIRECT_ALIGN - 1) & ~(size_t)(DIRECT_ALIGN - 1);
	if (posix_memalign(&p, DIRECT_ALIGN, rounded ? rounded : DIRECT_ALIGN))
		return NULL;
	return p;
}

static int ring_setup(struct fu_io *io)
{
	struct io_uring_params p;
	memset(&p, 0, sizeof(p));
	/* Cheaper completion handling on 6.x kernels; retry plain if refused. */
	p.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN;
	int r = io_uring_queue_init_params((unsigned)io->depth, &io->ring, &p);
	if (r < 0) {
		memset(&p, 0, sizeof(p));
		r = io_uring_queue_init_params((unsigned)io->depth, &io->ring, &p);
	}
	io->ring_ok = r == 0;
	return r;
}

struct fu_io *fu_io_open(const char *path, int flags, mode_t mode, int backend, int depth)
{
	if (depth < 1)
		depth = 1;
	if (depth > 4096)
		depth = 4096;
	struct fu_io *io = calloc(1, sizeof(*io));
	if (!io)
		return NULL;
	io->fd = open(path, flags | O_CLOEXEC, mode);
	if (io->fd < 0) {
		free(io);
		return NULL;
	}
	io->dfd = -1;
	io->flags = flags & ~(O_CREAT | O_TRUNC | O_EXCL);
	io->path = strdup(path);
	io->backend = backend;
	io->depth = depth;
	io->last = FU_BACKEND_NORMAL;
	if (backend != FU_BACKEND_NORMAL)
		ring_setup(io); /* failure => everything degrades to NORMAL */
	return io;
}

int fu_io_close(struct fu_io *io)
{
	if (!io)
		return 0;
	if (io->ring_ok)
		io_uring_queue_exit(&io->ring);
	if (io->dfd >= 0)
		close(io->dfd);
	int r = close(io->fd);
	free(io->path);
	free(io);
	return r;
}

int fu_io_fsync(struct fu_io *io)
{
	return fdatasync(io->fd) < 0 ? -errno : 0;
}

int fu_io_backend_last(const struct fu_io *io)
{
	return io->last;
}

void fu_io_get_stats(const struct fu_io *io, struct fu_io_stats *st)
{
	*st = io->st;
}

static int direct_fd(struct fu_io *io)
{
	if (io->dfd >= 0)
		return io->dfd;
	io->dfd = open(io->path, io->flags | O_DIRECT | O_CLOEXEC);
	return io->dfd;
}

static int aligned(const struct fu_req *r)
{
	return ((uintptr_t)r->buf % DIRECT_ALIGN) == 0 && (r->len % 512) == 0 &&
	       (r->off % 512) == 0;
}

/* ---- backends ---- */

static int run_sync(struct fu_io *io, struct fu_req *reqs, int n)
{
	int ok = 0;
	for (int i = 0; i < n; i++) {
		struct fu_req *r = &reqs[i];
		double t0 = now_us();
		ssize_t x = r->op == FU_OP_WRITE ? pwrite(io->fd, r->buf, r->len, r->off)
						 : pread(io->fd, r->buf, r->len, r->off);
		r->lat_us = now_us() - t0;
		r->res = x < 0 ? -errno : x;
		ok += x >= 0;
	}
	io->st.ops[FU_BACKEND_NORMAL] += (uint64_t)n;
	return ok;
}

static int run_uring(struct fu_io *io, struct fu_req *reqs, int n, int want_direct)
{
	int dfd = want_direct ? direct_fd(io) : -1;
	double t0 = now_us();
	int queued = 0;
	for (int i = 0; i < n; i++) {
		struct fu_req *r = &reqs[i];
		struct io_uring_sqe *sqe = io_uring_get_sqe(&io->ring);
		if (!sqe)
			break;
		int fd = io->fd;
		if (want_direct) {
			if (dfd >= 0 && aligned(r))
				fd = dfd;
			else
				io->st.direct_fallbacks++;
		}
		if (r->op == FU_OP_WRITE)
			io_uring_prep_write(sqe, fd, r->buf, (unsigned)r->len, (uint64_t)r->off);
		else
			io_uring_prep_read(sqe, fd, r->buf, (unsigned)r->len, (uint64_t)r->off);
		io_uring_sqe_set_data64(sqe, (uint64_t)i);
		queued++;
	}
	int s = io_uring_submit(&io->ring);
	if (s < 0)
		return s;
	int ok = 0;
	for (int done = 0; done < queued; done++) {
		struct io_uring_cqe *cqe;
		int w = io_uring_wait_cqe(&io->ring, &cqe);
		if (w < 0)
			return w;
		struct fu_req *r = &reqs[io_uring_cqe_get_data64(cqe)];
		r->res = cqe->res;
		r->lat_us = now_us() - t0;
		ok += cqe->res >= 0;
		io_uring_cqe_seen(&io->ring, cqe);
	}
	/* Short reads/writes are legal; finish them synchronously for correctness. */
	for (int i = 0; i < queued; i++) {
		struct fu_req *r = &reqs[i];
		if (r->res >= 0 && (size_t)r->res < r->len && r->op == FU_OP_WRITE) {
			ssize_t x = pwrite(io->fd, (char *)r->buf + r->res, r->len - (size_t)r->res,
					   r->off + r->res);
			if (x > 0)
				r->res += x;
		}
	}
	/* Anything that did not fit in the SQ runs synchronously. */
	if (queued < n)
		ok += run_sync(io, reqs + queued, n - queued);
	return ok;
}

int fu_io_run(struct fu_io *io, struct fu_req *reqs, int n)
{
	if (n <= 0)
		return 0;
	int b = io->backend;
	if (b == FU_BACKEND_AUTO) {
		int p = fu_current_policy(NULL, NULL);
		b = (p >= 0 && p <= FU_BACKEND_SPECIALIZED) ? p : FU_BACKEND_NORMAL;
	}
	if (b != FU_BACKEND_NORMAL && !io->ring_ok)
		b = FU_BACKEND_NORMAL;
	if (b != io->last && io->st.batches)
		io->st.backend_switches++;
	io->last = b;
	io->st.batches++;
	if (b == FU_BACKEND_NORMAL)
		return run_sync(io, reqs, n);
	io->st.ops[b] += (uint64_t)n;
	return run_uring(io, reqs, n, b == FU_BACKEND_SPECIALIZED);
}
