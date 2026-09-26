/* GDT + TSS. Layout is dictated by `syscall`/`sysret`:
 *   0x08 kernel code, 0x10 kernel data, 0x18 user data, 0x20 user code, 0x28 TSS.
 * The TSS supplies RSP0 (kernel stack on ring 3 -> 0 transitions) and IST
 * stacks for double faults and NMIs, so those never run on a broken stack. */
#include "arch/x86_64/idt.h"
#include "kernel.h"

struct PACKED tss {
	u32 rsvd0;
	u64 rsp[3];
	u64 rsvd1;
	u64 ist[7];
	u64 rsvd2;
	u16 rsvd3;
	u16 iomap_base;
};

struct PACKED gdtr {
	u16 limit;
	u64 base;
};

static u64 gdt[7];
static struct tss tss;
static u8 ist_df_stack[16384] __attribute__((aligned(16)));
static u8 ist_nmi_stack[16384] __attribute__((aligned(16)));

#define SEG(access, flags) (((u64)(flags) << 52) | ((u64)(access) << 40) | 0xFFFF | (0xFULL << 48))

void tss_set_kernel_stack(u64 rsp0) { tss.rsp[0] = rsp0; }

void gdt_init(void)
{
	gdt[0] = 0;
	gdt[1] = SEG(0x9A, 0xA);	/* kernel code: present, ring0, exec/read; L=1 */
	gdt[2] = SEG(0x92, 0xC);	/* kernel data */
	gdt[3] = SEG(0xF2, 0xC);	/* user data, ring3 */
	gdt[4] = SEG(0xFA, 0xA);	/* user code, ring3, L=1 */

	tss.iomap_base = sizeof(tss);
	tss.ist[0] = (u64)ist_df_stack + sizeof(ist_df_stack);
	tss.ist[1] = (u64)ist_nmi_stack + sizeof(ist_nmi_stack);
	u64 base = (u64)&tss, limit = sizeof(tss) - 1;
	gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ULL << 40) |
		 (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
	gdt[6] = base >> 32;

	struct gdtr r = { sizeof(gdt) - 1, (u64)gdt };
	__asm__ volatile(
		"lgdt %0\n"
		"pushq $0x08\n"
		"leaq 1f(%%rip), %%rax\n"
		"pushq %%rax\n"
		"lretq\n"
		"1:\n"
		"movw $0x10, %%ax\n"
		"movw %%ax, %%ds\n"
		"movw %%ax, %%es\n"
		"movw %%ax, %%ss\n"
		"xorw %%ax, %%ax\n"
		"movw %%ax, %%fs\n"
		"movw %%ax, %%gs\n"
		"movw $0x28, %%ax\n"
		"ltr %%ax\n"
		:
		: "m"(r)
		: "rax", "memory");
}
