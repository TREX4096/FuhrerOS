/* Kernel context switch.
 *   void context_switch(u64 *save_rsp, u64 new_rsp)
 * Saves the callee-saved registers on the current kernel stack, stores the
 * stack pointer, loads the new one and restores the next task's registers.
 * A brand-new task's stack is prepared so the final `ret` lands in
 * task_trampoline with r12 = entry function and r13 = argument. */
    .text
    .globl context_switch
context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

    .globl task_trampoline
task_trampoline:
    call sched_finish_switch
    sti
    movq %r13, %rdi
    xorq %rbp, %rbp
    call *%r12
    xorl %edi, %edi
    call task_exit_from_trampoline
    ud2
