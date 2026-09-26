/* Block devices and the buffer cache (with adaptive caching, M14). */
#ifndef FUHRER_BLK_H
#define FUHRER_BLK_H
#include "list.h"
#include "sched.h"
#include "types.h"

#define BLOCK_SIZE 4096
#define SECTORS_PER_BLOCK 8

struct blk_req {
	u64 sector;
	u32 count;		/* sectors */
	paddr_t buf;		/* physically contiguous DMA buffer */
	bool write;
	volatile bool done;
	int status;
	void (*complete)(struct blk_req *r);	/* called from IRQ context */
	void *ctx;
	struct waitqueue wait;
	u64 submit_ns;
};

struct blkdev {
	char name[16];
	u64 sectors;
	u32 sector_size;
	int (*submit)(struct blkdev *d, struct blk_req *r);
	void *priv;
	u32 id;
	/* stats */
	u64 reads, writes, read_bytes, write_bytes;
	u64 busy_ns;
	u32 inflight, max_inflight;
};

int blk_register(struct blkdev *d);
struct blkdev *blk_get(const char *name);
/* Synchronous helper: submit and sleep until done. */
int blk_rw(struct blkdev *d, u64 sector, u32 count, paddr_t buf, bool write);
int blk_submit(struct blkdev *d, struct blk_req *r);

/* ---- buffer cache ---- */
struct buf {
	struct blkdev *dev;
	u64 blockno;
	paddr_t phys;
	u8 *data;
	bool valid, dirty, readahead, referenced;
	int refcnt;
	struct blk_req req;
	volatile bool io_pending;
	struct list_node hash_node, lru_node;
	u64 last_use;
};

enum cache_policy { CACHE_LRU, CACHE_FIFO, CACHE_CLOCK, CACHE_READAHEAD, CACHE_ADAPTIVE,
		    CACHE_POLICY_COUNT };

void bcache_init(u32 capacity_blocks);
struct buf *bread(struct blkdev *d, u64 blockno);	/* file data: referenced + valid */
struct buf *bread_meta(struct blkdev *d, u64 blockno);	/* metadata: not a data stream */
struct buf *bget_nofill(struct blkdev *d, u64 blockno);	/* caller overwrites whole block */
void bdirty(struct buf *b);
void brelse(struct buf *b);
void bsync(struct blkdev *d);
int bcache_set_policy(const char *name);
const char *bcache_policy_name(void);
void bcache_set_capacity(u32 blocks);
void bcache_drop(void);		/* write back + invalidate everything */
void bcache_reset_stats(void);
struct bcache_stats {
	u64 hits, misses, readahead_issued, readahead_used, evictions, writebacks;
	u32 blocks, capacity;
	const char *io_class;
};
void bcache_get_stats(struct bcache_stats *s);
#endif
