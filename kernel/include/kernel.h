/* FuhrerOS kernel — core declarations shared by every subsystem. */
#ifndef FUHRER_KERNEL_H
#define FUHRER_KERNEL_H

#include "lib.h"
#include "types.h"

#define FUHREROS_VERSION "0.1.0"
#define FUHREROS_CODENAME "from-scratch"

/* ---- console / logging ---- */
void printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void vprintk(const char *fmt, va_list ap);
void console_write(const char *s, size_t n);
/* Output sinks: serial is always on; the framebuffer console attaches later. */
void console_set_fb(bool on);

#define KLOG(tag, fmt, ...) printk("[%-6s] " fmt "\n", tag, ##__VA_ARGS__)

/* ---- panic ---- */
struct trap_frame;
NORETURN void panic(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
NORETURN void panic_trap(const char *reason, struct trap_frame *tf);
void stack_trace(u64 rbp, int max);

#define ASSERT(c)                                                                 \
	do {                                                                      \
		if (unlikely(!(c)))                                               \
			panic("assertion failed: %s (%s:%d)", #c, __FILE__, __LINE__); \
	} while (0)

/* ---- boot information recorded from the bootloader ---- */
#define BOOT_MAX_MMAP 128
#define BOOT_MAX_MODULES 8

enum mem_type {
	MEM_USABLE,
	MEM_RESERVED,
	MEM_ACPI_RECLAIM,
	MEM_ACPI_NVS,
	MEM_BAD,
	MEM_BOOTLOADER_RECLAIM,
	MEM_KERNEL_AND_MODULES,
	MEM_FRAMEBUFFER,
};

struct boot_mmap_entry {
	u64 base, length;
	u32 type;
};

struct boot_module {
	paddr_t phys;
	void *virt;
	u64 size;
	char path[64];
	char cmdline[64];
};

struct boot_info {
	char bootloader[48];
	char cmdline[128];
	u64 hhdm_offset;
	paddr_t kernel_phys;
	vaddr_t kernel_virt;
	u64 kernel_size;
	paddr_t rsdp;			/* physical */
	i64 boot_timestamp;		/* UNIX seconds from firmware RTC */
	bool uefi;
	/* framebuffer */
	paddr_t fb_phys;
	void *fb_virt;
	u32 fb_width, fb_height, fb_pitch, fb_bpp;
	u8 fb_rshift, fb_gshift, fb_bshift;
	/* memory */
	struct boot_mmap_entry mmap[BOOT_MAX_MMAP];
	u32 mmap_count;
	u64 usable_bytes;
	paddr_t max_phys;
	/* modules (initrd) */
	struct boot_module modules[BOOT_MAX_MODULES];
	u32 module_count;
	/* cpu */
	char cpu_vendor[16];
	char cpu_brand[52];
};

extern struct boot_info boot;
bool boot_cmdline_has(const char *word);

static inline void *phys_to_virt(paddr_t p) { return (void *)(p + boot.hhdm_offset); }
static inline paddr_t virt_to_phys_hhdm(const void *v) { return (paddr_t)v - boot.hhdm_offset; }

/* Kernel image bounds from the linker script. */
extern char __kernel_start[], __kernel_end[], __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[], __data_start[], __bss_end[];

/* ---- time ---- */
u64 time_ns(void);		/* monotonic since boot (TSC based) */
u64 time_ms(void);
u64 ticks(void);		/* scheduler timer ticks */
i64 time_unix(void);		/* wall clock seconds */
void delay_us(u64 us);

/* ---- self tests ---- */
void selftest_report(const char *name, bool ok, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
bool selftest_mode(void);
NORETURN void qemu_exit(int code);

#endif
