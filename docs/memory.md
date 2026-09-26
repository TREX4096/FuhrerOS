# Memory management

## Physical memory manager (`mm/pmm.c`)
Bitmap allocator, one bit per 4 KiB frame over the whole physical range. The
bitmap is placed in the first usable region that fits. API:
`pmm_alloc_frame()` (zeroed), `pmm_alloc_frames(n)` (contiguous, for DMA),
`pmm_free_frame(s)()`, `pmm_reserve_region()`, `pmm_reclaim_bootloader()`.
The first MiB is never handed out. Double frees panic.

## Virtual memory manager (`mm/vmm.c`)
Kernel-built 4-level page tables (the bootloader's are discarded):
- HHDM: every memory-map region, 2 MiB pages where aligned, write-back;
  the framebuffer write-combining (PAT index 5).
- Kernel image mapped per section: text R-X, rodata R--, data/bss RW-+NX.
- CR0.WP (read-only enforced in ring 0), CR4.PGE (global kernel pages),
  EFER.NXE.
- API: `vmm_map_page`, `vmm_unmap_page`, `vmm_translate`, `vmm_protect_page`,
  `vmm_map_anon`, `ioremap` (uncached MMIO), `vmalloc_pages`,
  `kstack_alloc` (with an unmapped guard page), user address spaces
  (`vmm_create_user_space` copies the 256 kernel PML4 entries).

## Kernel heap (`mm/heap.c`)
Size classes 16–2048 B served from 4 KiB slab pages (header at the start of
each page, free list inside); larger requests are page runs from vmalloc with
a 64-byte header (so `kfree` distinguishes them by pointer alignment).

## Measured (self-test, KVM, 2 vCPU guest)
From `scripts/test.sh` on the current build:

| test | result |
|---|---|
| 256 frames alloc/free, uniqueness, zeroing | PASS, free count restored |
| 64 contiguous frames | PASS |
| map / write / translate / protect / unmap | PASS |
| 2 000 mixed allocations (1 B–4 KiB + 40 × 20 KiB) | no overlap; slab utilisation 54 % |

Allocation latency for that mix averaged ~130 µs because the 40 large
allocations each map five fresh, zeroed frames; small-object allocation is
far cheaper but is not yet measured separately (**NOT RUN**).
