/* `syscall` entry and ring-3 entry.
 *
 * On `syscall` the CPU saves RIP in RCX and RFLAGS in R11, loads CS/SS from
 * STAR and masks RFLAGS with SFMASK (IF cleared). We swap to the kernel GS,
 * switch to the task's kernel stack, and build a struct trap_frame identical
 * to the interrupt path's, so syscalls, faults and signals share one frame
 * format. Return is through iretq (restores RFLAGS/RSP/RIP atomically). */

    .text
    .globl syscall_entry
syscall_entry:
    swapgs
    movq %rsp, %gs:8           /* percpu.user_rsp */
    movq %gs:0, %rsp           /* percpu.kernel_rsp */
    pushq $0x1b                /* user SS (GDT_UDATA | 3) */
    pushq %gs:8                /* user RSP */
    pushq %r11                 /* user RFLAGS */
    pushq $0x23                /* user CS (GDT_UCODE | 3) */
    pushq %rcx                 /* user RIP */
    pushq $0                   /* error */
    pushq $0x80                /* "vector": marks a syscall frame */
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
    pushq 136(%rsp)            /* frame record for stack traces */
    pushq %rbp
    movq %rsp, %rbp
    sti                        /* syscalls are preemptible */
    call syscall_dispatch
    cli
    addq $16, %rsp
    jmp isr_return             /* shared exit path: pops regs, swapgs, iretq */

/* enter_user(rip, rsp, arg0, arg1): first entry of a thread into ring 3. */
    .globl enter_user
enter_user:
    cli
    pushq $0x1b                /* SS */
    pushq %rsi                 /* RSP */
    pushq $0x202               /* RFLAGS: IF set */
    pushq $0x23                /* CS */
    pushq %rdi                 /* RIP */
    movq %rdx, %rdi            /* arg0 */
    movq %rcx, %rsi            /* arg1 */
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %ebp, %ebp
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    xorl %r12d, %r12d
    xorl %r13d, %r13d
    xorl %r14d, %r14d
    xorl %r15d, %r15d
    swapgs
    iretq
