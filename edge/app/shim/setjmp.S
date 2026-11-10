/* x86-64 setjmp/longjmp for the freestanding PE build (Win64 ABI).
 * jmp_buf: [rbx rbp rdi rsi r12 r13 r14 r15 rsp rip] as 10 u64 slots.
 * mquickjs uses these for JS parse-error recovery inside JS_Eval. */
    .text
    .globl setjmp
setjmp:
    movq %rbx, 0(%rcx)
    movq %rbp, 8(%rcx)
    movq %rdi, 16(%rcx)
    movq %rsi, 24(%rcx)
    movq %r12, 32(%rcx)
    movq %r13, 40(%rcx)
    movq %r14, 48(%rcx)
    movq %r15, 56(%rcx)
    leaq 8(%rsp), %rax
    movq %rax, 64(%rcx)
    movq (%rsp), %rax
    movq %rax, 72(%rcx)
    xorl %eax, %eax
    ret

    .globl longjmp
longjmp:
    movq 0(%rcx), %rbx
    movq 8(%rcx), %rbp
    movq 16(%rcx), %rdi
    movq 24(%rcx), %rsi
    movq 32(%rcx), %r12
    movq 40(%rcx), %r13
    movq 48(%rcx), %r14
    movq 56(%rcx), %r15
    movq 64(%rcx), %rsp
    movq 72(%rcx), %rax
    movq %rdx, %rcx
    testq %rcx, %rcx
    jne 1f
    movq $1, %rcx
1:
    movq %rcx, %rax
    jmp *%rax

    .globl _setjmp
_setjmp:
    jmp setjmp
