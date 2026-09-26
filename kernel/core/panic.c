/* Kernel panic: dump CPU state and a symbolised stack trace, then halt.
 * Output goes to serial first so it survives a broken framebuffer. */
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/idt.h"
#include "kernel.h"

const char *ksym_lookup(u64 addr, u64 *offset);

/* Provided by the scheduler once it exists; defaults keep early panics safe. */
__attribute__((weak)) const char *sched_current_task_name(void) { return "(boot)"; }
__attribute__((weak)) const char *sched_current_process_name(void) { return "(kernel)"; }
__attribute__((weak)) int sched_current_tid(void) { return 0; }

static volatile int in_panic;

void stack_trace(u64 rbp, int max)
{
	printk("Stack trace:\n");
	for (int i = 0; i < max && rbp; i++) {
		/* Only follow frames inside the kernel's higher half. */
		if (rbp < 0xffff800000000000ULL || (rbp & 7))
			break;
		u64 *frame = (u64 *)rbp;
		u64 ret = frame[1];
		if (ret < (u64)__text_start || ret >= (u64)__text_end)
			break;
		u64 off = 0;
		const char *name = ksym_lookup(ret, &off);
		printk("  #%d %p %s+0x%lx\n", i, (void *)ret, name ? name : "?", off);
		rbp = frame[0];
	}
}

static void header(const char *reason)
{
	cli();
	if (in_panic++) {
		for (;;)
			hlt();
	}
	printk("\n\033[1;31m================================\n");
	printk("      FUHREROS KERNEL PANIC\n");
	printk("================================\033[0m\n\n");
	printk("Reason:          %s\n", reason);
	printk("CPU:             0 (bootstrap processor)\n");
}

static void footer(void)
{
	printk("Current task:    %s (tid %d)\n", sched_current_task_name(), sched_current_tid());
	printk("Current process: %s\n", sched_current_process_name());
}

NORETURN static void die(void)
{
	printk("\nSystem halted.\n");
	if (selftest_mode())
		qemu_exit(3);
	for (;;)
		hlt();
}

NORETURN void panic(const char *fmt, ...)
{
	char reason[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(reason, sizeof(reason), fmt, ap);
	va_end(ap);
	header(reason);
	u64 rbp;
	__asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
	printk("RIP:             %p\n", __builtin_return_address(0));
	printk("RSP:             %p\n", __builtin_frame_address(0));
	printk("CR2:             %p\n", (void *)read_cr2());
	printk("Error code:      n/a\n");
	footer();
	stack_trace(rbp, 16);
	die();
}

NORETURN void panic_trap(const char *reason, struct trap_frame *tf)
{
	header(reason);
	u64 off = 0;
	const char *sym = ksym_lookup(tf->rip, &off);
	printk("RIP:             %p %s+0x%lx\n", (void *)tf->rip, sym ? sym : "?", off);
	printk("RSP:             %p\n", (void *)tf->rsp);
	printk("CR2:             %p\n", (void *)read_cr2());
	printk("Error code:      0x%lx (vector %lu)\n", tf->error, tf->vector);
	footer();
	printk("RAX=%p RBX=%p RCX=%p RDX=%p\n", (void *)tf->rax, (void *)tf->rbx, (void *)tf->rcx,
	       (void *)tf->rdx);
	printk("RSI=%p RDI=%p RBP=%p CR3=%p\n", (void *)tf->rsi, (void *)tf->rdi, (void *)tf->rbp,
	       (void *)read_cr3());
	printk("CS=%lx SS=%lx RFLAGS=%lx\n", tf->cs, tf->ss, tf->rflags);
	stack_trace(tf->rbp, 16);
	die();
}
