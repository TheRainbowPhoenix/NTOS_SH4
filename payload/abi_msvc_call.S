! Shared native GCC -> Microsoft SH4 callable runtime (integer words only).
    .section .text
    .align 2
! GCC signature: u32 ntos_msvc_call_words(void *target,u32 *args,u32 n).
! Outgoing MSVC stack: SP+0..15 writable homes; fifth SP+16.
    .align 2
    .global _ntos_msvc_call_words
_ntos_msvc_call_words:
    ! The shared frame has exactly 8 argument words (32 bytes).
    ! Fail closed on unsupported arities; never corrupt the caller stack.
    mov #5,r0
    cmp/eq r6,r0
    bt .Larity_ok
    mov #8,r0
    cmp/eq r6,r0
    bt .Larity_ok
    mov.l .Larity_error,r0
    rts
    nop
.Larity_ok:
    tst r4,r4
    bf .Ltarget_ok
    mov.l .Larity_error,r0
    rts
    nop
.Ltarget_ok:
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    stc.l gbr,@-r15
    sts.l pr,@-r15
    mov r4,r8
    mov r5,r9
    mov r6,r10
    add #-32,r15
    mov.l @r9+,r4
    mov.l @r9+,r5
    mov.l @r9+,r6
    mov.l @r9+,r7
    mov r15,r1
    add #16,r1
    mov r10,r2
    add #-4,r2
.Lcall_copy:
    tst r2,r2
    bt .Lcall_done
    mov.l @r9+,r0
    mov.l r0,@r1
    add #4,r1
    add #-1,r2
    bra .Lcall_copy
    nop
.Lcall_done:
    mov r8,r0
    jsr @r0
    nop
    add #32,r15
    lds.l @r15+,pr
    ldc.l @r15+,gbr
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop


    .align 2
.Larity_error: .long 0xBADAB100
