/*
 * libfuhrer correctness: every backend must read back exactly what any
 * other backend wrote (policy switches must never change data).
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fuhrer.h"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
	fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define BS 4096
#define NB 64

int main(void)
{
	char path[] = "./fuhrer-io-test-XXXXXX";
	int tfd = mkstemp(path);
	CHECK(tfd >= 0);
	CHECK(ftruncate(tfd, BS * NB) == 0);
	close(tfd);

	CHECK(fu_backend_from_name("specialized") == FU_BACKEND_SPECIALIZED);
	CHECK(fu_backend_from_name("auto") == FU_BACKEND_AUTO);
	CHECK(fu_backend_from_name("nope") == -1);

	/* With no daemon, AUTO must degrade to NORMAL. */
	setenv("FUHRER_SHM", "/nonexistent/state.shm", 1);
	CHECK(fu_current_policy(NULL, NULL) == -1);

	struct fu_req reqs[NB];
	char *bufs[NB];
	for (int i = 0; i < NB; i++)
		bufs[i] = fu_io_alloc(BS);

	for (int wb = 0; wb <= FU_BACKEND_AUTO; wb++) {
		struct fu_io *w = fu_io_open(path, O_RDWR, 0, wb, NB);
		CHECK(w != NULL);
		for (int i = 0; i < NB; i++) {
			memset(bufs[i], (wb * 31 + i) & 0xff, BS);
			reqs[i] = (struct fu_req){ .op = FU_OP_WRITE, .buf = bufs[i], .len = BS,
						   .off = (off_t)i * BS };
		}
		CHECK(fu_io_run(w, reqs, NB) == NB);
		for (int i = 0; i < NB; i++)
			CHECK(reqs[i].res == BS && reqs[i].lat_us >= 0);
		CHECK(fu_io_fsync(w) == 0);
		fu_io_close(w);

		for (int rb = 0; rb <= FU_BACKEND_SPECIALIZED; rb++) {
			struct fu_io *r = fu_io_open(path, O_RDONLY, 0, rb, NB);
			for (int i = 0; i < NB; i++) {
				memset(bufs[i], 0, BS);
				reqs[i] = (struct fu_req){ .op = FU_OP_READ, .buf = bufs[i],
							   .len = BS, .off = (off_t)i * BS };
			}
			CHECK(fu_io_run(r, reqs, NB) == NB);
			int bad = 0;
			for (int i = 0; i < NB; i++)
				for (int k = 0; k < BS; k++)
					bad += (unsigned char)bufs[i][k] != ((wb * 31 + i) & 0xff);
			CHECK(bad == 0);
			if (bad)
				fprintf(stderr, "  write=%s read=%s mismatched bytes=%d\n",
					fu_backend_name(wb), fu_backend_name(rb), bad);
			struct fu_io_stats st;
			fu_io_get_stats(r, &st);
			CHECK(st.batches == 1);
			fu_io_close(r);
		}
	}

	/* Unaligned buffer on the specialized path must fall back, not fail. */
	struct fu_io *r = fu_io_open(path, O_RDONLY, 0, FU_BACKEND_SPECIALIZED, 4);
	char *raw = malloc(BS + 1);
	struct fu_req q = { .op = FU_OP_READ, .buf = raw + 1, .len = 100, .off = 3 };
	CHECK(fu_io_run(r, &q, 1) == 1 && q.res == 100);
	fu_io_close(r);
	free(raw);

	unlink(path);
	printf("libfuhrer unit tests: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
