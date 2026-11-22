/* SH4AL-DSP + integer C-ABI register canary. Not an IRQ trap-frame.
 * In r4: yield function, r5: per-thread seed. Returns r0=1 on success.
 * Preserves original guest r8-r14, PR and X0/Y0/M0.
 * ASSEMBLE WITH -Wa,-dsp, CPU SH7305 HAS NO FPU.
 */
    .section .text
    .align 2
    .global _kreg_yield_checked
    .type _kreg_yield_checked,@function
_kreg_yield_checked:
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    mov r15,r2
    movs.l x0,@-r2
    mov r2,r15
    mov r15,r2
    movs.l y0,@-r2
    mov r2,r15
    mov r15,r2
    movs.l m0,@-r2
    mov r2,r15
    mov r4,r14
    mov r5,r13
    mov r5,r8
    mov r5,r9
    add #1,r9
    mov r5,r10
    add #2,r10
    mov r5,r11
    add #3,r11
    mov r5,r12
    add #4,r12
    mov.l r13,@-r15
    mov r15,r2
    movs.l @r2+,x0
    mov r2,r15
    mov r13,r0
    add #1,r0
    mov.l r0,@-r15
    mov r15,r2
    movs.l @r2+,y0
    mov r2,r15
    mov r13,r0
    add #2,r0
    mov.l r0,@-r15
    mov r15,r2
    movs.l @r2+,m0
    mov r2,r15

    jsr @r14
    nop
    tst r0,r0
    bf 2f
    cmp/eq r13,r8
    bf 2f
    mov r13,r0
    add #1,r0
    cmp/eq r9,r0
    bf 2f
    mov r13,r0
    add #2,r0
    cmp/eq r10,r0
    bf 2f
    mov r13,r0
    add #3,r0
    cmp/eq r11,r0
    bf 2f
    mov r13,r0
    add #4,r0
    cmp/eq r12,r0
    bf 2f

    mov r15,r2
    movs.l x0,@-r2
    mov r2,r15
    mov.l @r15+,r0
    cmp/eq r13,r0
    bf 2f
    mov r15,r2
    movs.l y0,@-r2
    mov r2,r15
    mov.l @r15+,r0
    mov r13,r1
    add #1,r1
    cmp/eq r1,r0
    bf 2f
    mov r15,r2
    movs.l m0,@-r2
    mov r2,r15
    mov.l @r15+,r0
    mov r13,r1
    add #2,r1
    cmp/eq r1,r0
    bf 2f
    mov #1,r0
    bra 3f
    nop
2:
    mov #0,r0
3:
    mov r15,r2
    movs.l @r2+,m0
    mov r2,r15
    mov r15,r2
    movs.l @r2+,y0
    mov r2,r15
    mov r15,r2
    movs.l @r2+,x0
    mov r2,r15
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    rts
    nop
    .size _kreg_yield_checked,.-_kreg_yield_checked
