/* Limine boot protocol handoff: every request the kernel makes lives here,
 * and the responses are copied into `struct boot_info` so no other code
 * depends on the bootloader's data structures (or on its memory, which is
 * reclaimed later). */
#include "arch/x86_64/cpu.h"
#include "kernel.h"
#include "limine.h"

#define REQ __attribute__((used, section(".limine_requests")))

__attribute__((used, section(".limine_requests_start")))
static volatile u64 requests_start[4] = LIMINE_REQUESTS_START_MARKER;
REQ static volatile u64 base_revision[3] = LIMINE_BASE_REVISION(6);

REQ static volatile struct limine_bootloader_info_request req_info = {
	.id = LIMINE_BOOTLOADER_INFO_REQUEST_ID, .revision = 0 };
REQ static volatile struct limine_executable_cmdline_request req_cmdline = {
	.id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID, .revision = 0 };
REQ static volatile struct limine_firmware_type_request req_fw = {
	.id = LIMINE_FIRMWARE_TYPE_REQUEST_ID, .revision = 0 };
REQ static volatile struct limine_hhdm_request req_hhdm = {
	.id = LIMINE_HHDM_REQUEST_ID, .revision = 0 };
REQ static volatile struct limine_framebuffer_request req_fb = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0 };
REQ static volatile struct limine_memmap_request req_mmap = {
	.id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0 };
REQ static volatile struct limine_module_request req_modules = {
	.id = LIMINE_MODULE_REQUEST_ID, .revision = 0 };
REQ static volatile struct limine_rsdp_request req_rsdp = {
	.id = LIMINE_RSDP_REQUEST_ID, .revision = 0 };
REQ static volatile struct limine_executable_address_request req_kaddr = {
	.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID, .revision = 0 };
REQ static volatile struct limine_date_at_boot_request req_date = {
	.id = LIMINE_DATE_AT_BOOT_REQUEST_ID, .revision = 0 };

__attribute__((used, section(".limine_requests_end")))
static volatile u64 requests_end[2] = LIMINE_REQUESTS_END_MARKER;

struct boot_info boot;

bool boot_revision_supported(void) { return LIMINE_BASE_REVISION_SUPPORTED(base_revision); }

static const u32 type_map[] = {
	[LIMINE_MEMMAP_USABLE] = MEM_USABLE,
	[LIMINE_MEMMAP_RESERVED] = MEM_RESERVED,
	[LIMINE_MEMMAP_ACPI_RECLAIMABLE] = MEM_ACPI_RECLAIM,
	[LIMINE_MEMMAP_ACPI_NVS] = MEM_ACPI_NVS,
	[LIMINE_MEMMAP_BAD_MEMORY] = MEM_BAD,
	[LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE] = MEM_BOOTLOADER_RECLAIM,
	[LIMINE_MEMMAP_EXECUTABLE_AND_MODULES] = MEM_KERNEL_AND_MODULES,
	[LIMINE_MEMMAP_FRAMEBUFFER] = MEM_FRAMEBUFFER,
	[LIMINE_MEMMAP_RESERVED_MAPPED] = MEM_RESERVED,
};

static void read_cpu_ids(void)
{
	u32 a, b, c, d;
	cpuid(0, 0, &a, &b, &c, &d);
	memcpy(boot.cpu_vendor + 0, &b, 4);
	memcpy(boot.cpu_vendor + 4, &d, 4);
	memcpy(boot.cpu_vendor + 8, &c, 4);
	boot.cpu_vendor[12] = 0;
	cpuid(0x80000000, 0, &a, &b, &c, &d);
	if (a >= 0x80000004) {
		u32 *p = (u32 *)boot.cpu_brand;
		for (u32 leaf = 0; leaf < 3; leaf++)
			cpuid(0x80000002 + leaf, 0, &p[leaf * 4], &p[leaf * 4 + 1], &p[leaf * 4 + 2],
			      &p[leaf * 4 + 3]);
		boot.cpu_brand[48] = 0;
		/* trim leading spaces */
		char *s = boot.cpu_brand;
		while (*s == ' ')
			s++;
		memmove(boot.cpu_brand, s, strlen(s) + 1);
	}
}

/* Returns false if the bootloader did not provide what the kernel needs. */
bool boot_parse(void)
{
	if (!req_hhdm.response || !req_mmap.response)
		return false;
	boot.hhdm_offset = req_hhdm.response->offset;
	if (req_info.response)
		snprintf(boot.bootloader, sizeof(boot.bootloader), "%s %s",
			 req_info.response->name, req_info.response->version);
	if (req_cmdline.response && req_cmdline.response->cmdline)
		strlcpy(boot.cmdline, req_cmdline.response->cmdline, sizeof(boot.cmdline));
	if (req_fw.response)
		boot.uefi = req_fw.response->firmware_type != LIMINE_FIRMWARE_TYPE_X86BIOS;
	if (req_kaddr.response) {
		boot.kernel_phys = req_kaddr.response->physical_base;
		boot.kernel_virt = req_kaddr.response->virtual_base;
	}
	boot.kernel_size = (u64)(__kernel_end - __kernel_start);
	if (req_rsdp.response)
		/* Virtual (HHDM) for every base revision except 3. */
		boot.rsdp = (paddr_t)req_rsdp.response->address - boot.hhdm_offset;
	if (req_date.response)
		boot.boot_timestamp = req_date.response->timestamp;

	struct limine_memmap_response *mm = req_mmap.response;
	for (u64 i = 0; i < mm->entry_count && boot.mmap_count < BOOT_MAX_MMAP; i++) {
		struct limine_memmap_entry *e = mm->entries[i];
		struct boot_mmap_entry *b = &boot.mmap[boot.mmap_count++];
		b->base = e->base;
		b->length = e->length;
		b->type = e->type < ARRAY_LEN(type_map) ? type_map[e->type] : MEM_RESERVED;
		if (b->type == MEM_USABLE)
			boot.usable_bytes += e->length;
		if (b->type != MEM_RESERVED && e->base + e->length > boot.max_phys)
			boot.max_phys = e->base + e->length;
	}

	if (req_fb.response && req_fb.response->framebuffer_count) {
		struct limine_framebuffer *fb = req_fb.response->framebuffers[0];
		boot.fb_virt = fb->address;
		boot.fb_phys = (paddr_t)fb->address - boot.hhdm_offset;
		boot.fb_width = (u32)fb->width;
		boot.fb_height = (u32)fb->height;
		boot.fb_pitch = (u32)fb->pitch;
		boot.fb_bpp = fb->bpp;
		boot.fb_rshift = fb->red_mask_shift;
		boot.fb_gshift = fb->green_mask_shift;
		boot.fb_bshift = fb->blue_mask_shift;
	}

	if (req_modules.response) {
		struct limine_module_response *m = req_modules.response;
		for (u64 i = 0; i < m->module_count && boot.module_count < BOOT_MAX_MODULES; i++) {
			struct limine_file *f = m->modules[i];
			struct boot_module *bm = &boot.modules[boot.module_count++];
			bm->virt = f->address;
			bm->phys = (paddr_t)f->address - boot.hhdm_offset;
			bm->size = f->size;
			strlcpy(bm->path, f->path ? f->path : "", sizeof(bm->path));
			strlcpy(bm->cmdline, f->string ? f->string : "", sizeof(bm->cmdline));
		}
	}
	read_cpu_ids();
	return true;
}

bool boot_cmdline_has(const char *word)
{
	size_t wl = strlen(word);
	for (const char *p = boot.cmdline; *p;) {
		while (*p == ' ')
			p++;
		const char *e = p;
		while (*e && *e != ' ')
			e++;
		if ((size_t)(e - p) == wl && !strncmp(p, word, wl))
			return true;
		p = e;
	}
	return false;
}

static const char *mem_type_name(u32 t)
{
	static const char *n[] = { "usable", "reserved", "acpi-reclaim", "acpi-nvs", "bad",
				   "bootloader", "kernel+modules", "framebuffer" };
	return t < ARRAY_LEN(n) ? n[t] : "?";
}

void boot_dump(void)
{
	KLOG("boot", "bootloader: %s (%s firmware)", boot.bootloader, boot.uefi ? "UEFI" : "BIOS");
	KLOG("boot", "cmdline: '%s'", boot.cmdline);
	KLOG("boot", "kernel: phys %p virt %p size %lu KiB", (void *)boot.kernel_phys,
	     (void *)boot.kernel_virt, boot.kernel_size / 1024);
	KLOG("boot", "hhdm offset: %p, rsdp: %p", (void *)boot.hhdm_offset, (void *)boot.rsdp);
	KLOG("boot", "memory map (%u entries):", boot.mmap_count);
	for (u32 i = 0; i < boot.mmap_count; i++)
		KLOG("boot", "  %016lx-%016lx %-14s %lu KiB", boot.mmap[i].base,
		     boot.mmap[i].base + boot.mmap[i].length, mem_type_name(boot.mmap[i].type),
		     boot.mmap[i].length / 1024);
	KLOG("boot", "usable memory: %lu MiB", boot.usable_bytes >> 20);
	if (boot.fb_virt)
		KLOG("boot", "framebuffer: %ux%u %ubpp pitch %u at phys %p", boot.fb_width,
		     boot.fb_height, boot.fb_bpp, boot.fb_pitch, (void *)boot.fb_phys);
	for (u32 i = 0; i < boot.module_count; i++)
		KLOG("boot", "module: %s (%lu bytes) '%s'", boot.modules[i].path, boot.modules[i].size,
		     boot.modules[i].cmdline);
	KLOG("boot", "cpu: %s / %s", boot.cpu_vendor, boot.cpu_brand);
}
