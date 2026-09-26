/* FuhrerOS kernel entry and staged initialisation (NEW_EXPLANATION §9). */
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/serial.h"
#include "gfx/fb.h"
#include "kernel.h"
#include "mm.h"

bool boot_parse(void);
bool boot_revision_supported(void);
void boot_dump(void);
void selftest_summary(void);
int selftest_failures(void);
void test_interrupts(void);
void test_memory(void);
void kernel_late_init(void);

/* The bootloader's stack lives in reclaimable memory; the kernel moves to
 * its own boot stack before anything else. */
static u8 boot_stack[64 * 1024] __attribute__((aligned(16)));

static void banner(void)
{
	printk("\n================================\n");
	printk("        FUHREROS KERNEL\n");
	printk("================================\n\n");
	printk("Architecture : x86_64\n");
	printk("Bootloader   : %s\n", boot.bootloader[0] ? boot.bootloader : "Limine");
	printk("Kernel       : FuhrerOS %s (%s)\n", FUHREROS_VERSION, FUHREROS_CODENAME);
	printk("Status       : BOOTED\n\n");
}

/* Stage: CPU sanity checks. */
static void cpu_sanity(void)
{
	u32 a, b, c, d;
	cpuid(0x80000001, 0, &a, &b, &c, &d);
	bool lm = d & (1u << 29), nx = d & (1u << 20);
	cpuid(1, 0, &a, &b, &c, &d);
	bool apic = d & (1u << 9), tsc = d & (1u << 4);
	if (!lm || !apic || !tsc)
		panic("unsupported CPU (long mode %d, APIC %d, TSC %d)", lm, apic, tsc);
	if (selftest_mode())
		selftest_report("cpu.features", true, "lm=%d nx=%d apic=%d tsc=%d", lm, nx, apic, tsc);
}

/* Global C++ constructors (scheduler policies are C++ objects). */
typedef void (*ctor_t)(void);
extern ctor_t __init_array_start[], __init_array_end[];
static void run_constructors(void)
{
	for (ctor_t *c = __init_array_start; c < __init_array_end; c++)
		(*c)();
}

NORETURN static void kmain_stage2(void)
{
	fb_init();
	fbcon_init();
	banner();
	boot_dump();
	cpu_sanity();
	if (selftest_mode()) {
		selftest_report("boot.banner", true, "kernel=FuhrerOS-%s", FUHREROS_VERSION);
		selftest_report("boot.memmap", boot.mmap_count > 0 && boot.usable_bytes > (64u << 20),
				"%u entries, %lu MiB usable", boot.mmap_count, boot.usable_bytes >> 20);
		selftest_report("boot.framebuffer", boot.fb_virt != NULL, "%ux%u", boot.fb_width,
				boot.fb_height);
	}

	pmm_init();		/* physical frame allocator */
	gdt_init();		/* GDT + TSS */
	idt_init();		/* IDT + exception handlers */
	vmm_init();		/* our own page tables */
	heap_init();		/* kmalloc/kfree */
	pmm_reclaim_bootloader(); /* nothing of the bootloader is used any more */
	run_constructors();

	acpi_init();
	pic_disable();
	lapic_init();
	ioapic_init();
	time_init();
	timer_irq_setup(1000);
	sti();

	if (selftest_mode()) {
		test_interrupts();
		test_memory();
	}
	kernel_late_init();	/* scheduler, devices, filesystems, user space */
	for (;;)
		hlt();
}

/* Default late init until the scheduler exists (overridden in core/init.c). */
__attribute__((weak)) void kernel_late_init(void)
{
	if (selftest_mode()) {
		selftest_summary();
		qemu_exit(selftest_failures() ? 2 : 1);
	}
	printk("FuhrerOS: early kernel up; halting (no scheduler yet).\n");
}

NORETURN void kmain(void)
{
	cli();
	serial_init();
	printk("\nFuhrerOS: kernel entry reached (serial up)\n");
	if (!boot_revision_supported())
		panic("bootloader does not support Limine base revision 6");
	if (!boot_parse())
		panic("bootloader did not provide HHDM and memory map");
	u64 top = (u64)boot_stack + sizeof(boot_stack);
	__asm__ volatile("mov %0, %%rsp\n"
			 "xor %%rbp, %%rbp\n"
			 "call *%1\n" ::"r"(top),
			 "r"(kmain_stage2)
			 : "memory");
	__builtin_unreachable();
}
