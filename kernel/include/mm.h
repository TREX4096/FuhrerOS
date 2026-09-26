/* FuhrerOS memory management: physical frames, page tables, kernel heap. */
#ifndef FUHRER_MM_H
#define FUHRER_MM_H

#include "types.h"

/* ---- physical memory manager (bitmap) ---- */
void pmm_init(void);
paddr_t pmm_alloc_frame(void);			/* zeroed frame, 0 on OOM */
paddr_t pmm_alloc_frames(u64 count);		/* physically contiguous, zeroed */
void pmm_free_frame(paddr_t p);
void pmm_free_frames(paddr_t p, u64 count);
void pmm_reserve_region(paddr_t base, u64 len);
void pmm_reclaim_bootloader(void);
struct pmm_stats {
	u64 total_frames, free_frames, used_frames, reserved_frames;
	u64 allocs, frees;
};
void pmm_get_stats(struct pmm_stats *s);

/* ---- virtual memory manager ---- */
#define PTE_P (1ULL << 0)
#define PTE_W (1ULL << 1)
#define PTE_U (1ULL << 2)
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#define PTE_A (1ULL << 5)
#define PTE_D (1ULL << 6)
#define PTE_PS (1ULL << 7)	/* huge page (in PD/PDPT); PAT bit in a PTE */
#define PTE_G (1ULL << 8)
#define PTE_NX (1ULL << 63)
#define PTE_ADDR 0x000FFFFFFFFFF000ULL

/* Mapping flags for the portable API */
#define VM_READ 0x0
#define VM_WRITE 0x1
#define VM_EXEC 0x2
#define VM_USER 0x4
#define VM_UNCACHED 0x8
#define VM_WC 0x10

#define KHEAP_BASE 0xffffc00000000000ULL
#define KHEAP_SIZE (64ULL << 30)
#define KSTACK_BASE 0xffffc80000000000ULL
#define MMIO_BASE 0xffffd00000000000ULL
#define USER_TOP 0x0000800000000000ULL

struct address_space {
	paddr_t pml4;
	u64 user_pages;		/* for accounting */
};

void vmm_init(void);
struct address_space *vmm_kernel_space(void);
int vmm_map_page(struct address_space *as, vaddr_t va, paddr_t pa, u32 flags);
int vmm_unmap_page(struct address_space *as, vaddr_t va, bool free_frame);
paddr_t vmm_translate(struct address_space *as, vaddr_t va);	/* 0 if unmapped */
int vmm_protect_page(struct address_space *as, vaddr_t va, u32 flags);
void vmm_switch(struct address_space *as);
struct address_space *vmm_create_user_space(void);
void vmm_destroy_user_space(struct address_space *as);
/* Map count fresh zeroed frames at va. */
int vmm_map_anon(struct address_space *as, vaddr_t va, u64 count, u32 flags);

void *ioremap(paddr_t phys, u64 size);				/* uncached MMIO */
void *vmalloc_pages(u64 count);				/* kernel VA, fresh frames */
void vfree_pages(void *p, u64 count);
void *kstack_alloc(u64 size, u64 *top);			/* guard page below */
void kstack_free(void *base, u64 size);

/* ---- kernel heap ---- */
void heap_init(void);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void kfree(void *p);
void *krealloc(void *p, size_t n);
struct heap_stats {
	u64 bytes_requested;	/* cumulative bytes asked for by callers */
	u64 bytes_slab;		/* live, rounded to size class */
	u64 slab_pages, large_pages;
	u64 allocs, frees;
};
void heap_get_stats(struct heap_stats *s);

#endif
