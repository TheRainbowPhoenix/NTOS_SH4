! Shared runtime experiment: MSVC->native entry and native->MSVC call.
! Only fixed 32-bit word arguments, arity 5 or 8. No FPU or varargs.
! MSVC SH4: r4-r7 first four words; incoming fifth at S+16;
! 16 caller-owned writable home bytes. GCC native fifth at S+0.
    .section .text
    .align 2
    .global _abi_msvc5
_abi_msvc5:
    mov.l .Ldesc5,r0
    bra .Lentry_shared
    nop
    .align 2
.Ldesc5:
    .long .Ldescriptor5
    .align 2
    .global _abi_msvc8
_abi_msvc8:
    mov.l .Ldesc8,r0
    bra .Lentry_shared
    nop
    .align 2
.Ldesc8:
    .long .Ldescriptor8

! r0 -> descriptor {pointer-to-target,word-count}, r4-r7 from MSVC.
! Keep original MSVC entry SP in a private array and invoke GCC dispatcher.
.Lentry_shared:
    mov.l r8,@-r15
    sts.l pr,@-r15
    mov r0,r8
    add #-32,r15
    ! Simulate MSVC /Od: the callee is allowed to overwrite all four
    ! incoming caller-provided home slots at entry S+0..S+15.
    mov r15,r1
    add #40,r1
    mov.l r4,@r1
    mov.l r5,@(4,r1)
    mov.l r6,@(8,r1)
    mov.l r7,@(12,r1)
    mov.l r4,@r15
    mov.l r5,@(4,r15)
    mov.l r6,@(8,r15)
    mov.l r7,@(12,r15)
    ! Entry S = current SP+40; fifth is S+16 = SP+56.
    mov r15,r1
    add #56,r1
    mov r15,r3
    add #16,r3
    mov.l @(4,r8),r2
    add #-4,r2
.Lentry_copy:
    tst r2,r2
    bt .Lentry_done
    mov.l @r1+,r0
    mov.l r0,@r3
    add #4,r3
    add #-1,r2
    bra .Lentry_copy
    nop
.Lentry_done:
    mov.l @r8,r4
    mov r15,r5
    mov.l @(4,r8),r6
    mov.l .Ldispatcher,r0
    jsr @r0
    nop
    add #32,r15
    lds.l @r15+,pr
    mov.l @r15+,r8
    rts
    nop

    .align 2
.Ldispatcher:
    .long _ntos_msvc_dispatch_native
.Ldescriptor5:
    .long _ntos_msvc_native5
    .long 5
.Ldescriptor8:
    .long _ntos_msvc_native8
    .long 8
