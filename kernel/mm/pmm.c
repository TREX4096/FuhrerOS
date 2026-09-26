/* Physical memory manager: one bit per 4 KiB frame (1 = used).
 * The bitmap itself is carved out of the first usable region that fits. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"

static u64 *bitmap;
static u64 total_frames;	/* frames covered by the bitmap */
static u64 free_frames;
static u64 usable_frames;
static u64 next_hint;		/* first possibly-free word */
static u64 n_allocs, n_frees;

static inline void set_used(u64 f) { bitmap[f / 64] |= 1ULL << (f % 64); }
static inline void set_free(u64 f) { bitmap[f / 64] &= ~(1ULL << (f % 64)); }
static inline bool is_used(u64 f) { return bitmap[f / 64] & (1ULL << (f % 64)); }

void pmm_init(void)
{
	total_frames = ALIGN_UP(boot.max_phys, PAGE_SIZE) / PAGE_SIZE;
	u64 bm_bytes = ALIGN_UP(ALIGN_UP(total_frames, 64) / 8, PAGE_SIZE);

	/* Place the bitmap in the first usable region large enough. */
	paddr_t bm_phys = 0;
	for (u32 i = 0; i < boot.mmap_count; i++) {
		struct boot_mmap_entry *e = &boot.mmap[i];
		if (e->type == MEM_USABLE && e->length >= bm_bytes && e->base >= 0x100000) {
			bm_phys = e->base;
			break;
		}
	}
	if (!bm_phys)
		panic("pmm: no region for a %lu byte frame bitmap", bm_bytes);
	bitmap = phys_to_virt(bm_phys);

	/* Everything starts used; usable regions are then released. */
	memset(bitmap, 0xFF, bm_bytes);
	for (u32 i = 0; i < boot.mmap_count; i++) {
		struct boot_mmap_entry *e = &boot.mmap[i];
		if (e->type != MEM_USABLE)
			continue;
		u64 first = ALIGN_UP(e->base, PAGE_SIZE) / PAGE_SIZE;
		u64 last = ALIGN_DOWN(e->base + e->length, PAGE_SIZE) / PAGE_SIZE;
		for (u64 f = first; f < last; f++) {
			set_free(f);
			free_frames++;
			usable_frames++;
		}
	}
	/* Never hand out the first MiB (real-mode leftovers, AP trampolines later). */
	pmm_reserve_region(0, 0x100000);
	pmm_reserve_region(bm_phys, bm_bytes);
	KLOG("pmm", "%lu frames tracked, %lu free (%lu MiB), bitmap %lu KiB at %p", total_frames,
	     free_frames, (free_frames * PAGE_SIZE) >> 20, bm_bytes / 1024, (void *)bm_phys);
}

void pmm_reserve_region(paddr_t base, u64 len)
{
	u64 f0 = base / PAGE_SIZE, f1 = ALIGN_UP(base + len, PAGE_SIZE) / PAGE_SIZE;
	u64 fl = irq_save();
	for (u64 f = f0; f < f1 && f < total_frames; f++) {
		if (!is_used(f)) {
			set_used(f);
			free_frames--;
		}
	}
	irq_restore(fl);
}

/* After switching to our own page tables, GDT and stack, the bootloader's
 * reclaimable memory (its page tables, GDT, request responses) is free. */
void pmm_reclaim_bootloader(void)
{
	u64 n = 0;
	u64 fl = irq_save();
	for (u32 i = 0; i < boot.mmap_count; i++) {
		struct boot_mmap_entry *e = &boot.mmap[i];
		if (e->type != MEM_BOOTLOADER_RECLAIM)
			continue;
		for (u64 f = e->base / PAGE_SIZE; f < (e->base + e->length) / PAGE_SIZE; f++) {
			if (is_used(f)) {
				set_free(f);
				free_frames++;
				usable_frames++;
				n++;
			}
		}
		e->type = MEM_USABLE;
	}
	next_hint = 0;
	irq_restore(fl);
	KLOG("pmm", "reclaimed %lu KiB of bootloader memory", n * 4);
}

static paddr_t alloc_one_locked(void)
{
	u64 words = ALIGN_UP(total_frames, 64) / 64;
	for (u64 k = 0; k < words; k++) {
		u64 w = (next_hint + k) % words;
		if (bitmap[w] == ~0ULL)
			continue;
		u64 bit = (u64)__builtin_ctzll(~bitmap[w]);
		u64 f = w * 64 + bit;
		if (f >= total_frames)
			continue;
		set_used(f);
		free_frames--;
		next_hint = w;
		return f * PAGE_SIZE;
	}
	return 0;
}

paddr_t pmm_alloc_frame(void)
{
	u64 fl = irq_save();
	paddr_t p = alloc_one_locked();
	if (p)
		n_allocs++;
	irq_restore(fl);
	if (p)
		memset(phys_to_virt(p), 0, PAGE_SIZE);
	return p;
}

paddr_t pmm_alloc_frames(u64 count)
{
	if (count == 1)
		return pmm_alloc_frame();
	u64 fl = irq_save();
	u64 run = 0, start = 0;
	for (u64 f = 256; f < total_frames; f++) {
		if (is_used(f)) {
			run = 0;
			continue;
		}
		if (run++ == 0)
			start = f;
		if (run == count) {
			for (u64 g = start; g < start + count; g++)
				set_used(g);
			free_frames -= count;
			n_allocs++;
			irq_restore(fl);
			memset(phys_to_virt(start * PAGE_SIZE), 0, count * PAGE_SIZE);
			return start * PAGE_SIZE;
		}
	}
	irq_restore(fl);
	return 0;
}

void pmm_free_frames(paddr_t p, u64 count)
{
	u64 fl = irq_save();
	for (u64 f = p / PAGE_SIZE; f < p / PAGE_SIZE + count; f++) {
		if (f < total_frames && is_used(f)) {
			set_free(f);
			free_frames++;
		} else {
			irq_restore(fl);
			panic("pmm: double free of frame %p", (void *)(f * PAGE_SIZE));
		}
	}
	if (p / PAGE_SIZE / 64 < next_hint)
		next_hint = p / PAGE_SIZE / 64;
	n_frees++;
	irq_restore(fl);
}

void pmm_free_frame(paddr_t p) { pmm_free_frames(p, 1); }

void pmm_get_stats(struct pmm_stats *s)
{
	s->total_frames = usable_frames;
	s->free_frames = free_frames;
	s->used_frames = usable_frames - free_frames;
	s->reserved_frames = total_frames - usable_frames;
	s->allocs = n_allocs;
	s->frees = n_frees;
}
