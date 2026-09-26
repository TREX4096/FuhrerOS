/* Virtual memory manager: 4-level x86-64 page tables built by the kernel
 * itself (the bootloader's tables are discarded).
 *
 * Kernel layout (upper half, shared by every address space):
 *   HHDM (bootloader offset)   all RAM, 2 MiB pages where possible, WB
 *   0xffffc000_00000000        kernel heap / vmalloc
 *   0xffffc800_00000000        kernel stacks (with guard pages)
 *   0xffffd000_00000000        MMIO (ioremap, uncached)
 *   0xffffffff_80000000        kernel image (text RX, rodata R, data RW NX)
 * The 256 upper PML4 slots are populated at boot so user address spaces can
 * simply copy them and still see later kernel mappings. */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "mm.h"

static struct address_space kernel_as;
static bool nx_ok;
static vaddr_t heap_brk = KHEAP_BASE;
static vaddr_t kstack_brk = KSTACK_BASE;
static vaddr_t mmio_brk = MMIO_BASE;

static inline u64 *table(paddr_t p) { return phys_to_virt(p); }
static inline int idx(vaddr_t va, int level) { return (int)((va >> (12 + 9 * level)) & 0x1FF); }

static u64 pte_flags(u32 f)
{
	u64 e = PTE_P;
	if (f & VM_WRITE)
		e |= PTE_W;
	if (f & VM_USER)
		e |= PTE_U;
	if (!(f & VM_EXEC) && nx_ok)
		e |= PTE_NX;
	if (f & VM_UNCACHED)
		e |= PTE_PCD | PTE_PWT;
	if (f & VM_WC)
		e |= PTE_PWT | PTE_PS; /* PAT index 5 = WC (PAT=1,PCD=0,PWT=1) in a 4K PTE */
	if (!(f & VM_USER))
		e |= PTE_G;
	return e;
}

/* Walk to the PTE for va, allocating intermediate tables if asked. */
static u64 *walk(struct address_space *as, vaddr_t va, bool create, bool user)
{
	u64 *t = table(as->pml4);
	for (int level = 3; level > 0; level--) {
		u64 *e = &t[idx(va, level)];
		if (!(*e & PTE_P)) {
			if (!create)
				return NULL;
			paddr_t f = pmm_alloc_frame();
			if (!f)
				return NULL;
			*e = f | PTE_P | PTE_W | (user ? PTE_U : 0);
		} else if (*e & PTE_PS) {
			return NULL; /* inside a huge page */
		} else if (user) {
			*e |= PTE_U;
		}
		t = table(*e & PTE_ADDR);
	}
	return &t[idx(va, 0)];
}

int vmm_map_page(struct address_space *as, vaddr_t va, paddr_t pa, u32 flags)
{
	u64 fl = irq_save();
	u64 *pte = walk(as, va, true, flags & VM_USER);
	if (!pte) {
		irq_restore(fl);
		return -E_NOMEM;
	}
	*pte = (pa & PTE_ADDR) | pte_flags(flags);
	invlpg(va);
	irq_restore(fl);
	return 0;
}

int vmm_unmap_page(struct address_space *as, vaddr_t va, bool free_frame)
{
	u64 fl = irq_save();
	u64 *pte = walk(as, va, false, false);
	if (!pte || !(*pte & PTE_P)) {
		irq_restore(fl);
		return -E_INVAL;
	}
	paddr_t pa = *pte & PTE_ADDR;
	*pte = 0;
	invlpg(va);
	irq_restore(fl);
	if (free_frame)
		pmm_free_frame(pa);
	return 0;
}

paddr_t vmm_translate(struct address_space *as, vaddr_t va)
{
	u64 *t = table(as->pml4);
	for (int level = 3; level >= 0; level--) {
		u64 e = t[idx(va, level)];
		if (!(e & PTE_P))
			return 0;
		if (level > 0 && (e & PTE_PS)) {
			u64 size = 1ULL << (12 + 9 * level);
			return (e & PTE_ADDR & ~(size - 1)) | (va & (size - 1));
		}
		if (level == 0)
			return (e & PTE_ADDR) | (va & 0xFFF);
		t = table(e & PTE_ADDR);
	}
	return 0;
}

int vmm_protect_page(struct address_space *as, vaddr_t va, u32 flags)
{
	u64 *pte = walk(as, va, false, false);
	if (!pte || !(*pte & PTE_P))
		return -E_INVAL;
	*pte = (*pte & PTE_ADDR) | pte_flags(flags);
	invlpg(va);
	return 0;
}

int vmm_map_anon(struct address_space *as, vaddr_t va, u64 count, u32 flags)
{
	for (u64 i = 0; i < count; i++) {
		paddr_t f = pmm_alloc_frame();
		if (!f || vmm_map_page(as, va + i * PAGE_SIZE, f, flags) < 0)
			return -E_NOMEM;
	}
	return 0;
}

/* Map [pa, pa+len) at va with 2 MiB pages where alignment allows. */
static void map_range_huge(vaddr_t va, paddr_t pa, u64 len, u64 extra)
{
	u64 end = pa + len;
	while (pa < end) {
		if (!(pa & 0x1FFFFF) && !(va & 0x1FFFFF) && end - pa >= 0x200000) {
			u64 *t = table(kernel_as.pml4);
			for (int level = 3; level > 1; level--) {
				u64 *e = &t[idx(va, level)];
				if (!(*e & PTE_P))
					*e = pmm_alloc_frame() | PTE_P | PTE_W;
				t = table(*e & PTE_ADDR);
			}
			t[idx(va, 1)] = pa | PTE_P | PTE_W | PTE_PS | PTE_G | (nx_ok ? PTE_NX : 0) | extra;
			pa += 0x200000;
			va += 0x200000;
		} else {
			u64 *pte = walk(&kernel_as, va, true, false);
			if (pte) /* NULL only if already covered by a huge page */
				*pte = pa | PTE_P | PTE_W | PTE_G | (nx_ok ? PTE_NX : 0) | extra;
			pa += PAGE_SIZE;
			va += PAGE_SIZE;
		}
	}
}

static void map_kernel_section(char *start, char *end, u32 flags)
{
	vaddr_t v0 = ALIGN_DOWN((vaddr_t)start, PAGE_SIZE);
	vaddr_t v1 = ALIGN_UP((vaddr_t)end, PAGE_SIZE);
	for (vaddr_t v = v0; v < v1; v += PAGE_SIZE)
		vmm_map_page(&kernel_as, v, v - boot.kernel_virt + boot.kernel_phys, flags);
}

void vmm_init(void)
{
	u32 a, b, c, d;
	cpuid(0x80000001, 0, &a, &b, &c, &d);
	nx_ok = d & (1u << 20);
	/* CR0.WP (enforce read-only in ring 0) and CR4.PGE (global pages). */
	write_cr0(read_cr0() | (1 << 16));
	write_cr4(read_cr4() | (1 << 7));

	kernel_as.pml4 = pmm_alloc_frame();
	u64 *pml4 = table(kernel_as.pml4);
	for (int i = 256; i < 512; i++)
		pml4[i] = pmm_alloc_frame() | PTE_P | PTE_W;

	/* HHDM: every memory-map region except bad RAM; framebuffer write-combined. */
	for (u32 i = 0; i < boot.mmap_count; i++) {
		struct boot_mmap_entry *e = &boot.mmap[i];
		if (e->type == MEM_BAD)
			continue;
		paddr_t base = ALIGN_DOWN(e->base, PAGE_SIZE);
		u64 len = ALIGN_UP(e->base + e->length, PAGE_SIZE) - base;
		if (e->type == MEM_FRAMEBUFFER) {
			for (u64 off = 0; off < len; off += PAGE_SIZE)
				vmm_map_page(&kernel_as, boot.hhdm_offset + base + off, base + off,
					     VM_WRITE | VM_WC);
		} else {
			map_range_huge(boot.hhdm_offset + base, base, len, 0);
		}
	}
	/* The framebuffer may sit outside the memory map (e.g. some firmware). */
	if (boot.fb_phys && !vmm_translate(&kernel_as, (vaddr_t)boot.fb_virt)) {
		u64 len = ALIGN_UP((u64)boot.fb_pitch * boot.fb_height, PAGE_SIZE);
		for (u64 off = 0; off < len; off += PAGE_SIZE)
			vmm_map_page(&kernel_as, (vaddr_t)boot.fb_virt + off, boot.fb_phys + off,
				     VM_WRITE | VM_WC);
	}

	map_kernel_section((char *)boot.kernel_virt, __text_start, VM_READ); /* requests */
	map_kernel_section(__text_start, __text_end, VM_EXEC);
	map_kernel_section(__rodata_start, __rodata_end, VM_READ);
	map_kernel_section(__data_start, __kernel_end, VM_WRITE);

	vmm_switch(&kernel_as);
	/* Enable NX usage (EFER.NXE) — Limine sets it when available; keep it on. */
	if (nx_ok)
		wrmsr(MSR_EFER, rdmsr(MSR_EFER) | (1 << 11));
	KLOG("vmm", "kernel page tables active (pml4 %p, NX %s)", (void *)kernel_as.pml4,
	     nx_ok ? "on" : "unavailable");
}

struct address_space *vmm_kernel_space(void) { return &kernel_as; }

void vmm_switch(struct address_space *as)
{
	if (read_cr3() != as->pml4)
		write_cr3(as->pml4);
}

struct address_space *vmm_create_user_space(void)
{
	struct address_space *as = kzalloc(sizeof(*as));
	if (!as)
		return NULL;
	as->pml4 = pmm_alloc_frame();
	if (!as->pml4) {
		kfree(as);
		return NULL;
	}
	u64 *dst = table(as->pml4), *src = table(kernel_as.pml4);
	for (int i = 256; i < 512; i++)
		dst[i] = src[i];
	return as;
}

static void free_level(paddr_t t, int level)
{
	u64 *e = table(t);
	for (int i = 0; i < 512; i++) {
		if (!(e[i] & PTE_P))
			continue;
		if (level == 0)
			pmm_free_frame(e[i] & PTE_ADDR);
		else
			free_level(e[i] & PTE_ADDR, level - 1);
	}
	pmm_free_frame(t);
}

void vmm_destroy_user_space(struct address_space *as)
{
	u64 *pml4 = table(as->pml4);
	for (int i = 0; i < 256; i++)
		if (pml4[i] & PTE_P)
			free_level(pml4[i] & PTE_ADDR, 2);
	pmm_free_frame(as->pml4);
	kfree(as);
}

void *ioremap(paddr_t phys, u64 size)
{
	paddr_t base = ALIGN_DOWN(phys, PAGE_SIZE);
	u64 len = ALIGN_UP(phys + size, PAGE_SIZE) - base;
	u64 fl = irq_save();
	vaddr_t va = mmio_brk;
	mmio_brk += len + PAGE_SIZE;
	irq_restore(fl);
	for (u64 off = 0; off < len; off += PAGE_SIZE)
		vmm_map_page(&kernel_as, va + off, base + off, VM_WRITE | VM_UNCACHED);
	return (void *)(va + (phys - base));
}

void *vmalloc_pages(u64 count)
{
	u64 fl = irq_save();
	vaddr_t va = heap_brk;
	heap_brk += count * PAGE_SIZE;
	irq_restore(fl);
	if (heap_brk > KHEAP_BASE + KHEAP_SIZE)
		panic("vmalloc: kernel heap VA exhausted");
	if (vmm_map_anon(&kernel_as, va, count, VM_WRITE) < 0)
		return NULL;
	return (void *)va;
}

void vfree_pages(void *p, u64 count)
{
	for (u64 i = 0; i < count; i++)
		vmm_unmap_page(&kernel_as, (vaddr_t)p + i * PAGE_SIZE, true);
	/* VA is not recycled (64 GiB of heap VA is plenty for the prototype). */
}

void *kstack_alloc(u64 size, u64 *top)
{
	u64 pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
	u64 fl = irq_save();
	vaddr_t va = kstack_brk + PAGE_SIZE; /* unmapped guard page below */
	kstack_brk += (pages + 1) * PAGE_SIZE;
	irq_restore(fl);
	if (vmm_map_anon(&kernel_as, va, pages, VM_WRITE) < 0)
		return NULL;
	*top = va + pages * PAGE_SIZE;
	return (void *)va;
}

void kstack_free(void *base, u64 size)
{
	vfree_pages(base, ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE);
}
