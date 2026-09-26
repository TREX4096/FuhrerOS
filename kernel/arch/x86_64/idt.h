/* x86-64 descriptor tables and interrupt entry. */
#ifndef ARCH_IDT_H
#define ARCH_IDT_H

#include "types.h"

/* Layout pushed by isr_common (arch/x86_64/isr.S) — keep in sync. */
struct trap_frame {
	u64 r15, r14, r13, r12, r11, r10, r9, r8;
	u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
	u64 vector, error;
	u64 rip, cs, rflags, rsp, ss;	/* pushed by the CPU */
};

#define GDT_KCODE 0x08
#define GDT_KDATA 0x10
#define GDT_UDATA 0x18	/* sysret requires udata = ucode - 8 */
#define GDT_UCODE 0x20
#define GDT_TSS 0x28

/* Interrupt vectors */
#define VEC_TIMER 0x20
#define VEC_KEYBOARD 0x21
#define VEC_MOUSE 0x2C
#define VEC_COM1 0x24
#define VEC_MSI_BASE 0x40	/* 0x40..0x7F for device MSI/MSI-X */
#define VEC_RESCHED 0xF0
#define VEC_SPURIOUS 0xFF

typedef void (*irq_handler_t)(struct trap_frame *tf, void *ctx);

void gdt_init(void);
void tss_set_kernel_stack(u64 rsp0);
void idt_init(void);
void irq_register(u8 vector, irq_handler_t h, void *ctx);
int irq_alloc_vector(void);	/* for MSI/MSI-X devices */
u64 irq_count(u8 vector);

#endif
