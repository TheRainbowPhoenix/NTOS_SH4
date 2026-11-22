/* KTRAPREG: real SH4AL-DSP no-FPU user TRAPA #0x7A frame canary.
 * Reads user bank0 r1-r7 after the audited P1 VBR -> RTE bridge.
 * The Ke system service replaces bank0 r0 with current PID only.
 * No scheduler/preemption is performed here.
 */
    .section .text
    .align 2
    .global _ktrap_regs_probe
    .type _ktrap_regs_probe,@function
_ktrap_regs_probe:
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
    mov r4,r8
    mov r5,r9
    mov r5,r10
    mov r5,r11
    add #1,r11
    mov r5,r12
    add #2,r12
    mov r5,r13
    add #3,r13
    mov r5,r14
    add #4,r14
    ! Preserve original DSP, then set 3 seeded DSP canaries.
    mov.l r9,@-r15
    mov r15,r2
    movs.l @r2+,x0
    mov r2,r15
    mov r11,r0
    mov.l r0,@-r15
    mov r15,r2
    movs.l @r2+,y0
    mov r2,r15
    mov r12,r0
    mov.l r0,@-r15
    mov r15,r2
    movs.l @r2+,m0
    mov r2,r15

    mov r9,r1
    mov r11,r2
    mov r12,r3
    mov r13,r6
    mov r14,r7
    mov.l .Lsvc,r4
    mov r9,r5
    mov.l .Lstub,r0
    jsr @r0
    nop
    cmp/eq r8,r0
    bf .Lbad
    cmp/eq r9,r1
    bf .Lbad
    cmp/eq r11,r2
    bf .Lbad
    cmp/eq r12,r3
    bf .Lbad
    mov.l .Lsvc,r0
    cmp/eq r4,r0
    bf .Lbad
    cmp/eq r9,r5
    bf .Lbad
    cmp/eq r13,r6
    bf .Lbad
    cmp/eq r14,r7
    bf .Lbad
    mov r15,r2
    movs.l x0,@-r2
    mov r2,r15
    mov.l @r15+,r0
    cmp/eq r9,r0
    bf .Lbad
    mov r15,r2
    movs.l y0,@-r2
    mov r2,r15
    mov.l @r15+,r0
    cmp/eq r11,r0
    bf .Lbad
    mov r15,r2
    movs.l m0,@-r2
    mov r2,r15
    mov.l @r15+,r0
    cmp/eq r12,r0
    bf .Lbad
    mov #1,r0
    bra .Lrestore
    nop
.Lbad:
    mov #0,r0
.Lrestore:
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
    .align 2
.Lstub: .long _ktrap_regs_user
.Lsvc:  .long 0x00001000
    .size _ktrap_regs_probe,.-_ktrap_regs_probe

/* Switch to user mode for ONE verified 0x7A instruction. Ke RTE
 * resumes privileged at RTS. Recreate the expected bank0 r1 seed after
 * the SR manipulation, which temporarily used r1 as a mask. */
    .global _ktrap_regs_user
    .global _ktrap_regs_trapa
    .align 2
_ktrap_regs_user:
    stc sr,r0
    mov.l .Lsr_mask,r1
    and r1,r0
    mov.l .Lsr_irq,r1
    or r1,r0
    ldc r0,sr
    nop
    mov r5,r1
_ktrap_regs_trapa:
    trapa #0x7a
    rts
    nop
    .align 2
.Lsr_mask: .long 0x8fffff0f
.Lsr_irq:  .long 0x000000f0
