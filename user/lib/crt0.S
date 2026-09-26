/* User program entry: the kernel starts threads with rdi = argc, rsi = argv
 * and rsp ≡ 8 (mod 16), exactly as if _start had been called. */
    .text
    .globl _start
_start:
    xorl %ebp, %ebp
    movq %rdi, %r12            /* argc (callee-saved across the init call) */
    movq %rsi, %r13            /* argv */
    andq $-16, %rsp
    call __libfu_init
    movq %r12, %rdi
    movq %r13, %rsi
    call main
    movl %eax, %edi
    call exit
    ud2

/* long syscall6(nr, a, b, c, d, e) */
    .globl syscall6
syscall6:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    syscall
    ret
