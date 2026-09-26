/* Interrupt/exception entry stubs. Every vector pushes a uniform frame
 * (see struct trap_frame in idt.h) and calls isr_dispatch(frame). */

.macro ISR_NOERR n
    .globl isr_stub_\n
isr_stub_\n:
    pushq $0
    pushq $\n
    jmp isr_common
.endm

.macro ISR_ERR n
    .globl isr_stub_\n
isr_stub_\n:
    pushq $\n
    jmp isr_common
.endm

    .text
isr_common:
    cld
    /* Arriving from ring 3: switch to the kernel GS (per-CPU area). */
    testb $3, 24(%rsp)
    jz 1f
    swapgs
1:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    /* Build a frame record so stack traces walk through interrupts. */
    pushq 136(%rsp)          /* interrupted RIP */
    pushq %rbp
    movq %rsp, %rbp
    call isr_dispatch
    addq $16, %rsp
    .globl isr_return
isr_return:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp          /* vector + error code */
    testb $3, 8(%rsp)
    jz 2f
    swapgs
2:
    iretq

/* Exceptions 0-31 (error code pushed by the CPU for 8,10-14,17,21,29,30). */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

/* Hardware / software vectors 32-255. */
.altmacro
.set i, 32
.rept 224
    ISR_NOERR %i
    .set i, i+1
.endr

/* Table of stub addresses consumed by idt_init(). */
.macro STUB_ADDR n
    .quad isr_stub_\n
.endm
    .section .rodata
    .balign 8
    .globl isr_stub_table
isr_stub_table:
.set i, 0
.rept 256
    STUB_ADDR %i
    .set i, i+1
.endr
