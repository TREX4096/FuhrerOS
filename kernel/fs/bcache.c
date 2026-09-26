/* Buffer cache with pluggable replacement / prefetch policies (M14).
 *
 * Blocks are 4 KiB, each backed by one physical frame (DMA-able). Policies:
 *   lru        evict least recently used
 *   fifo       evict oldest loaded
 *   clock      second chance (approximates a working set)
 *   readahead  LRU + sequential read-ahead (window 2 -> 32 blocks)
 *   adaptive   classifies the access stream every 256 accesses:
 *                SEQUENTIAL (>= 60 % next-block accesses): aggressive
 *                  read-ahead and evict-behind (used stream blocks go to the
 *                  eviction end, so a large scan cannot flush the working set)
 *                RANDOM (<= 20 %): no read-ahead; CLOCK if the hit rate says
 *                  there is a working set, else LRU
 *                MIXED: read-ahead window 4, LRU
 * Dirty blocks are written back on eviction, on sync(), and every 2 s by the
 * flusher thread. */
#include "arch/x86_64/cpu.h"
#include "blk.h"
#include "kernel.h"
#include "mm.h"

#define NHASH 1024

static const char *policy_names[CACHE_POLICY_COUNT] = { "lru", "fifo", "clock", "readahead",
							 "adaptive" };
static enum cache_policy policy = CACHE_ADAPTIVE;
static struct list_node hash[NHASH];
static struct list_node lru = LIST_INIT(lru); /* front = eviction end */
static u32 nbufs, capacity;
static u64 clock_tick;
static struct bcache_stats st;
static struct waitqueue bufio_wait;

/* adaptive state */
enum io_class { IO_UNKNOWN, IO_SEQUENTIAL, IO_RANDOM, IO_MIXED };
static const char *io_names[] = { "unknown", "sequential", "random", "mixed" };
static enum io_class io_cls = IO_UNKNOWN;
static u32 win_accesses, win_seq, win_hits;
static u64 last_block = ~0ULL;
static u32 ra_window = 2;
static u64 adapt_changes;
static u64 cache_cycles;

static u32 hidx(struct blkdev *d, u64 b) { return (u32)((b * 2654435761u) ^ d->id) % NHASH; }

static struct buf *lookup(struct blkdev *d, u64 b)
{
	list_for_each(it, &hash[hidx(d, b)]) {
		struct buf *x = container_of(it, struct buf, hash_node);
		if (x->dev == d && x->blockno == b)
			return x;
	}
	return NULL;
}

static void touch(struct buf *b)
{
	b->referenced = true;
	b->last_use = ++clock_tick;
	bool move_on_use = policy == CACHE_LRU || policy == CACHE_READAHEAD ||
			   (policy == CACHE_ADAPTIVE && io_cls != IO_RANDOM);
	if (policy == CACHE_ADAPTIVE && io_cls == IO_SEQUENTIAL && b->readahead) {
		/* evict-behind: a consumed stream block is unlikely to be re-read */
		list_remove(&b->lru_node);
		list_push_front(&lru, &b->lru_node);
		return;
	}
	if (move_on_use) {
		list_remove(&b->lru_node);
		list_push_back(&lru, &b->lru_node);
	}
}

static bool use_clock(void)
{
	return policy == CACHE_CLOCK || (policy == CACHE_ADAPTIVE && io_cls == IO_RANDOM &&
					 st.hits * 2 > st.misses);
}

static void writeback(struct buf *b)
{
	if (!b->dirty)
		return;
	b->dirty = false;
	st.writebacks++;
	blk_rw(b->dev, b->blockno * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, b->phys, true);
}

/* Find a reusable buffer (called with interrupts disabled; may sleep for
 * write-back, in which case it re-enables around the I/O). */
static struct buf *victim(void)
{
	if (nbufs < capacity) {
		struct buf *b = kzalloc(sizeof(*b));
		b->phys = pmm_alloc_frame();
		if (!b->phys) {
			kfree(b);
			goto evict;
		}
		b->data = phys_to_virt(b->phys);
		list_init(&b->hash_node);
		list_push_back(&lru, &b->lru_node);
		nbufs++;
		return b;
	}
evict:
	for (int pass = 0; pass < 3; pass++) {
		list_for_each_safe(it, tmp, &lru) {
			struct buf *b = container_of(it, struct buf, lru_node);
			if (b->refcnt || b->io_pending)
				continue;
			if (use_clock() && b->referenced && pass < 2) {
				b->referenced = false;
				list_remove(&b->lru_node);
				list_push_back(&lru, &b->lru_node);
				continue;
			}
			if (b->dirty) {
				/* write-back sleeps: pin it, and rescan afterwards because
				 * other tasks may have changed the list meanwhile */
				b->refcnt++;
				writeback(b);
				b->refcnt--;
				goto evict;
			}
			if (list_linked(&b->hash_node))
				list_remove(&b->hash_node);
			b->valid = false;
			b->readahead = false;
			st.evictions++;
			list_remove(&b->lru_node);
			list_push_back(&lru, &b->lru_node);
			return b;
		}
	}
	return NULL; /* everything pinned */
}

static void ra_complete(struct blk_req *r)
{
	struct buf *b = container_of(r, struct buf, req);
	b->valid = r->status == 0;
	b->io_pending = false;
	wq_wake_all(&bufio_wait);
}

static void readahead(struct blkdev *d, u64 from, u32 count)
{
	u64 max = d->sectors / SECTORS_PER_BLOCK;
	for (u32 i = 0; i < count && from + i < max; i++) {
		if (lookup(d, from + i))
			continue;
		struct buf *b = victim();
		if (!b)
			return;
		b->dev = d;
		b->blockno = from + i;
		b->readahead = true;
		b->referenced = false;
		b->io_pending = true;
		list_push_back(&hash[hidx(d, b->blockno)], &b->hash_node);
		/* prefetched blocks start at the cold end unless the stream uses them */
		list_remove(&b->lru_node);
		list_push_front(&lru, &b->lru_node);
		b->req = (struct blk_req){ .sector = b->blockno * SECTORS_PER_BLOCK,
					   .count = SECTORS_PER_BLOCK, .buf = b->phys,
					   .complete = ra_complete };
		st.readahead_issued++;
		if (blk_submit(d, &b->req) < 0) {
			b->io_pending = false;
			list_remove(&b->hash_node);
			return;
		}
	}
}

static void adapt_window(void)
{
	enum io_class c;
	u32 seq_pct = win_seq * 100 / win_accesses;
	if (seq_pct >= 60)
		c = IO_SEQUENTIAL;
	else if (seq_pct <= 20)
		c = IO_RANDOM;
	else
		c = IO_MIXED;
	if (c != io_cls) {
		adapt_changes++;
		if (boot_cmdline_has("adapt.log"))
			printk("[ADAPT-CACHE] %s -> %s (sequential %u%%, hits %u/%u)\n", io_names[io_cls],
			       io_names[c], seq_pct, win_hits, win_accesses);
		io_cls = c;
	}
	win_accesses = win_seq = win_hits = 0;
}

static void account_access(struct blkdev *d, u64 blockno, bool hit)
{
	bool seq = blockno == last_block + 1;
	last_block = blockno;
	win_accesses++;
	win_seq += seq;
	win_hits += hit;
	if (policy == CACHE_ADAPTIVE && win_accesses >= 256)
		adapt_window();
	u32 ra = 0;
	if (policy == CACHE_READAHEAD && seq) {
		ra_window = MIN(ra_window * 2, 32);
		ra = ra_window;
	} else if (policy == CACHE_READAHEAD) {
		ra_window = 2;
	} else if (policy == CACHE_ADAPTIVE) {
		if (io_cls == IO_SEQUENTIAL || (io_cls == IO_UNKNOWN && seq))
			ra = seq ? (ra_window = MIN(ra_window * 2, 32)) : 0;
		else if (io_cls == IO_MIXED && seq)
			ra = 4;
		if (!seq)
			ra_window = 2;
	}
	if (ra)
		readahead(d, blockno + 1, ra);
}

static struct buf *get(struct blkdev *d, u64 blockno, bool fill)
{
	u64 c0 = rdtsc();
	u64 f = irq_save();
	for (;;) {
		struct buf *b = lookup(d, blockno);
		if (b) {
			if (b->io_pending) {
				wq_wait(&bufio_wait, 0);
				continue; /* re-check after the read completed */
			}
			if (!b->valid) { /* failed read-ahead: reload below */
				list_remove(&b->hash_node);
			} else {
				b->refcnt++;
				st.hits++;
				if (b->readahead) {
					st.readahead_used++;
				}
				touch(b);
				account_access(d, blockno, true);
				b->readahead = policy == CACHE_ADAPTIVE && io_cls == IO_SEQUENTIAL ? true : false;
				cache_cycles += rdtsc() - c0;
				irq_restore(f);
				return b;
			}
		}
		b = victim();
		if (!b) {
			irq_restore(f);
			panic("bcache: all %u buffers pinned", nbufs);
		}
		b->dev = d;
		b->blockno = blockno;
		b->refcnt = 1;
		b->readahead = false;
		b->dirty = false;
		list_push_back(&hash[hidx(d, blockno)], &b->hash_node);
		st.misses++;
		touch(b);
		if (fill) {
			b->io_pending = true;
			irq_restore(f);
			int r = blk_rw(d, blockno * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, b->phys, false);
			f = irq_save();
			b->io_pending = false;
			b->valid = r == 0;
			wq_wake_all(&bufio_wait);
		} else {
			memset(b->data, 0, BLOCK_SIZE);
			b->valid = true;
		}
		account_access(d, blockno, false);
		cache_cycles += rdtsc() - c0;
		irq_restore(f);
		return b;
	}
}

struct buf *bread(struct blkdev *d, u64 blockno) { return get(d, blockno, true); }
struct buf *bget_nofill(struct blkdev *d, u64 blockno) { return get(d, blockno, false); }

void bdirty(struct buf *b) { b->dirty = true; }

void brelse(struct buf *b)
{
	u64 f = irq_save();
	if (b->refcnt > 0)
		b->refcnt--;
	irq_restore(f);
}

void bsync(struct blkdev *d)
{
	u64 f = irq_save();
	for (;;) {
		struct buf *dirty = NULL;
		list_for_each(it, &lru) {
			struct buf *b = container_of(it, struct buf, lru_node);
			if (b->dirty && (!d || b->dev == d) && !b->io_pending) {
				dirty = b;
				break;
			}
		}
		if (!dirty)
			break;
		dirty->refcnt++; /* the list may change while we sleep in I/O */
		writeback(dirty);
		dirty->refcnt--;
	}
	irq_restore(f);
}

void bcache_drop(void)
{
	bsync(NULL);
	u64 f = irq_save();
	list_for_each(it, &lru) {
		struct buf *b = container_of(it, struct buf, lru_node);
		if (!b->refcnt && !b->io_pending && list_linked(&b->hash_node)) {
			list_remove(&b->hash_node);
			b->valid = false;
		}
	}
	last_block = ~0ULL;
	irq_restore(f);
}

int bcache_set_policy(const char *name)
{
	for (int i = 0; i < CACHE_POLICY_COUNT; i++)
		if (!strcmp(policy_names[i], name)) {
			u64 f = irq_save();
			policy = (enum cache_policy)i;
			io_cls = IO_UNKNOWN;
			ra_window = 2;
			win_accesses = win_seq = win_hits = 0;
			irq_restore(f);
			KLOG("bcache", "policy -> %s", name);
			return 0;
		}
	return -E_INVAL;
}

const char *bcache_policy_name(void) { return policy_names[policy]; }

void bcache_set_capacity(u32 blocks)
{
	if (blocks < 64)
		blocks = 64;
	bcache_drop();
	u64 f = irq_save();
	capacity = blocks;
	/* shrink: free unpinned surplus buffers */
	list_for_each_safe(it, tmp, &lru) {
		if (nbufs <= capacity)
			break;
		struct buf *b = container_of(it, struct buf, lru_node);
		if (b->refcnt || b->io_pending || b->dirty)
			continue;
		if (list_linked(&b->hash_node))
			list_remove(&b->hash_node);
		list_remove(&b->lru_node);
		pmm_free_frame(b->phys);
		kfree(b);
		nbufs--;
	}
	irq_restore(f);
}

void bcache_reset_stats(void)
{
	u64 f = irq_save();
	memset(&st, 0, sizeof(st));
	cache_cycles = 0;
	irq_restore(f);
}

void bcache_get_stats(struct bcache_stats *s)
{
	*s = st;
	s->blocks = nbufs;
	s->capacity = capacity;
	s->io_class = io_names[io_cls];
}

static void flusher(void *arg)
{
	for (;;) {
		sleep_ms(2000);
		bsync(NULL);
	}
}

struct pbuf;
void pb_printf(struct pbuf *p, const char *fmt, ...);
void procfs_register(const char *name, void (*gen)(void *p));
u64 tsc_frequency(void);
static void gen_bcache(void *pb)
{
	u64 total = st.hits + st.misses;
	pb_printf(pb, "policy: %s\nio_class: %s\ncapacity_blocks: %u\nblocks: %u\n", policy_names[policy],
		  io_names[io_cls], capacity, nbufs);
	pb_printf(pb, "hits: %lu\nmisses: %lu\nhit_rate_pm: %lu\n", st.hits, st.misses,
		  total ? st.hits * 1000 / total : 0);
	pb_printf(pb, "readahead_issued: %lu\nreadahead_used: %lu\nevictions: %lu\nwritebacks: %lu\n",
		  st.readahead_issued, st.readahead_used, st.evictions, st.writebacks);
	pb_printf(pb, "memory_kb: %u\nadapt_changes: %lu\ncpu_ns: %lu\n", nbufs * 4, adapt_changes,
		  tsc_frequency() ? cache_cycles * 1000000000ULL / tsc_frequency() : 0);
}

void bcache_init(u32 cap)
{
	for (int i = 0; i < NHASH; i++)
		list_init(&hash[i]);
	wq_init(&bufio_wait);
	capacity = cap;
	if (boot_cmdline_has("cache=lru"))
		policy = CACHE_LRU;
	task_create_kernel("bflush", flusher, NULL);
	procfs_register("bcache", (void (*)(void *))gen_bcache);
	KLOG("bcache", "%u x 4 KiB buffers, policy %s", cap, policy_names[policy]);
}
