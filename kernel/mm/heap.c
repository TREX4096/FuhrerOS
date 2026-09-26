/* Kernel heap: size-class slabs for small objects, page runs for large ones.
 *
 * Small (<= 2048 B): each 4 KiB slab page starts with a header and serves
 * one size class; kfree finds the header by rounding the pointer down.
 * Large: a run of pages from the vmalloc region whose first 64 bytes hold a
 * header, so the returned pointer is never page aligned (which is how kfree
 * tells the two cases apart). */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"

#define SLAB_MAGIC 0x5AB5AB5AB5AB5AB5ULL
#define LARGE_MAGIC 0x1A26E1A26E1A26E1ULL
#define LARGE_HDR 64

static const u32 classes[] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };
#define NCLASS ARRAY_LEN(classes)

struct slab {
	u64 magic;
	struct slab *next;	/* slabs of this class with free objects */
	void *free;		/* free list of objects */
	u32 size;
	u32 in_use;
	u32 capacity;
	u32 cls;
};

struct large_hdr {
	u64 magic;
	u64 pages;
	u64 requested;
};

static struct slab *partial[NCLASS];
static struct heap_stats st;

static int class_of(size_t n)
{
	for (u32 i = 0; i < NCLASS; i++)
		if (n <= classes[i])
			return (int)i;
	return -1;
}

static struct slab *slab_new(int cls)
{
	struct slab *s = vmalloc_pages(1);
	if (!s)
		return NULL;
	s->magic = SLAB_MAGIC;
	s->size = classes[cls];
	s->cls = (u32)cls;
	u64 first = ALIGN_UP(sizeof(struct slab), s->size < 64 ? 16 : 64);
	s->capacity = (u32)((PAGE_SIZE - first) / s->size);
	s->in_use = 0;
	s->free = NULL;
	for (int i = (int)s->capacity - 1; i >= 0; i--) {
		void **obj = (void **)((u8 *)s + first + (u64)i * s->size);
		*obj = s->free;
		s->free = obj;
	}
	s->next = partial[cls];
	partial[cls] = s;
	st.slab_pages++;
	return s;
}

void heap_init(void)
{
	/* Warm one slab per class so the first allocations are cheap. */
	for (u32 i = 0; i < NCLASS; i++)
		slab_new((int)i);
	KLOG("heap", "slab classes 16..2048 B, large allocations from vmalloc");
}

void *kmalloc(size_t n)
{
	if (n == 0)
		n = 1;
	int cls = class_of(n);
	u64 fl = irq_save();
	void *p = NULL;
	if (cls >= 0) {
		struct slab *s = partial[cls];
		if (!s)
			s = slab_new(cls);
		if (s) {
			p = s->free;
			s->free = *(void **)p;
			s->in_use++;
			if (!s->free) /* full: drop from the partial list */
				partial[cls] = s->next;
			st.bytes_slab += s->size;
		}
	} else {
		u64 pages = ALIGN_UP(n + LARGE_HDR, PAGE_SIZE) / PAGE_SIZE;
		struct large_hdr *h = vmalloc_pages(pages);
		if (h) {
			h->magic = LARGE_MAGIC;
			h->pages = pages;
			h->requested = n;
			p = (u8 *)h + LARGE_HDR;
			st.large_pages += pages;
		}
	}
	if (p) {
		st.bytes_requested += n;
		st.allocs++;
	}
	irq_restore(fl);
	return p;
}

void *kzalloc(size_t n)
{
	void *p = kmalloc(n);
	if (p)
		memset(p, 0, n);
	return p;
}

static size_t alloc_size(void *p)
{
	if (((u64)p & (PAGE_SIZE - 1)) == LARGE_HDR) {
		struct large_hdr *h = (void *)((u8 *)p - LARGE_HDR);
		if (h->magic == LARGE_MAGIC)
			return h->requested;
	}
	struct slab *s = (void *)ALIGN_DOWN((u64)p, PAGE_SIZE);
	return s->size;
}

void kfree(void *p)
{
	if (!p)
		return;
	u64 fl = irq_save();
	if (((u64)p & (PAGE_SIZE - 1)) == LARGE_HDR) {
		struct large_hdr *h = (void *)((u8 *)p - LARGE_HDR);
		if (h->magic == LARGE_MAGIC) {
			h->magic = 0;
			st.large_pages -= h->pages;
			st.frees++;
			u64 pages = h->pages;
			irq_restore(fl);
			vfree_pages(h, pages);
			return;
		}
	}
	struct slab *s = (void *)ALIGN_DOWN((u64)p, PAGE_SIZE);
	if (s->magic != SLAB_MAGIC) {
		irq_restore(fl);
		panic("kfree: bad pointer %p", p);
	}
	bool was_full = s->free == NULL;
	*(void **)p = s->free;
	s->free = p;
	s->in_use--;
	if (was_full) {
		s->next = partial[s->cls];
		partial[s->cls] = s;
	}
	st.bytes_slab -= s->size;
	st.frees++;
	irq_restore(fl);
}

void *krealloc(void *p, size_t n)
{
	if (!p)
		return kmalloc(n);
	size_t old = alloc_size(p);
	if (n <= old)
		return p;
	void *q = kmalloc(n);
	if (q) {
		memcpy(q, p, old);
		kfree(p);
	}
	return q;
}

void heap_get_stats(struct heap_stats *s) { *s = st; }
