/* PSSTACK root canary. C ABI -> real SH4 r15 mapped P0 private
 * stack -> 32 push/pop round trips -> P1 host stack. The time spent on
 * P0 stack masks IRQ (IMASK=F), restoring original SR only AFTER P1
 * r15 is restored. This is privileged C-ABI, not user-mode RTE.
 */
    .section .text
    .align 2
    .global _psstack_probe
    .type _psstack_probe,@function
_psstack_probe:
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov r15,r8
    mov r4,r9
    mov r5,r15
    stc sr,r10
    mov r10,r0
    or #0xf0,r0
    ldc r0,sr
    nop
    mov #32,r1
.Lstack_loop:
    mov.l r9,@-r15
    mov.l @r15+,r2
    cmp/eq r9,r2
    bf .Lbad
    dt r1
    bf .Lstack_loop
    mov r9,r0
    bra .Ldone
    nop
.Lbad:
    mov #0,r0
.Ldone:
    /* No P0 SP use once interrupts are unmasked. */
    mov r8,r15
    ldc r10,sr
    nop
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    rts
    nop
    .size _psstack_probe,.-_psstack_probe
