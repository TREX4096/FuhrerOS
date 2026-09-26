/* IDT setup and interrupt dispatch. Exceptions from ring 3 terminate the
 * offending process; exceptions in the kernel panic with a full dump. */
#include "arch/x86_64/idt.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "kernel.h"

struct PACKED idt_entry {
	u16 off_lo;
	u16 sel;
	u8 ist;
	u8 flags;
	u16 off_mid;
	u32 off_hi;
	u32 zero;
};

static struct idt_entry idt[256];
extern const u64 isr_stub_table[256];

static struct {
	irq_handler_t fn;
	void *ctx;
} handlers[256];
static u64 counts[256];
static int next_msi_vector = VEC_MSI_BASE;

static const char *exc_names[32] = {
	"Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "BOUND range", "Invalid opcode",
	"Device not available", "Double fault", "Coprocessor overrun", "Invalid TSS",
	"Segment not present", "Stack-segment fault", "General protection fault", "Page fault",
	"Reserved", "x87 FP error", "Alignment check", "Machine check", "SIMD FP", "Virtualization",
	"Control protection", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
	"Reserved", "Hypervisor injection", "VMM communication", "Security", "Reserved",
};

static void set_gate(int v, u64 addr, u8 ist, u8 dpl)
{
	idt[v].off_lo = addr & 0xFFFF;
	idt[v].sel = GDT_KCODE;
	idt[v].ist = ist;
	idt[v].flags = 0x8E | (u8)(dpl << 5); /* present, interrupt gate */
	idt[v].off_mid = (addr >> 16) & 0xFFFF;
	idt[v].off_hi = (u32)(addr >> 32);
	idt[v].zero = 0;
}

void idt_init(void)
{
	for (int v = 0; v < 256; v++)
		set_gate(v, isr_stub_table[v], 0, 0);
	set_gate(8, isr_stub_table[8], 1, 0);	/* double fault on IST1 */
	set_gate(2, isr_stub_table[2], 2, 0);	/* NMI on IST2 */
	set_gate(3, isr_stub_table[3], 0, 3);	/* int3 allowed from user mode */
	struct PACKED {
		u16 limit;
		u64 base;
	} r = { sizeof(idt) - 1, (u64)idt };
	__asm__ volatile("lidt %0" ::"m"(r));
}

void irq_register(u8 vector, irq_handler_t h, void *ctx)
{
	handlers[vector].fn = h;
	handlers[vector].ctx = ctx;
}

int irq_alloc_vector(void)
{
	if (next_msi_vector >= 0x80)
		return -1;
	return next_msi_vector++;
}

u64 irq_count(u8 vector) { return counts[vector]; }

/* Hooks provided by higher layers (weak so early boot links without them). */
__attribute__((weak)) bool proc_handle_user_fault(struct trap_frame *tf) { return false; }
__attribute__((weak)) bool vmm_handle_page_fault(struct trap_frame *tf) { return false; }
__attribute__((weak)) void sched_irq_exit(struct trap_frame *tf) {}

void isr_dispatch(struct trap_frame *tf)
{
	u64 v = tf->vector;
	counts[v]++;
	if (v < 32) {
		if (handlers[v].fn) { /* e.g. self-tests hooking #BP */
			handlers[v].fn(tf, handlers[v].ctx);
			return;
		}
		if (v == 14 && vmm_handle_page_fault(tf))
			return;
		if ((tf->cs & 3) && proc_handle_user_fault(tf)) {
			sched_irq_exit(tf);
			return;
		}
		panic_trap(exc_names[v], tf);
	}
	if (handlers[v].fn)
		handlers[v].fn(tf, handlers[v].ctx);
	if (v != VEC_SPURIOUS)
		lapic_eoi();
	sched_irq_exit(tf);
}
