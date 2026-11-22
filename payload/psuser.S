/* PSUSER: 128 instructions groups genuinely execute in user SR.MD=0
 * on a mapped private P0 4KiB stack, with 128 VBR->Ke->RTE syscalls.
 * Only the exact loop and exit TRAPA addresses are armed in the kernel.
 * Saved privileged P1 SP never enters a user-mode PUSH/POP.
 */
    .section .text
    .align 2
    .global _psuser_run
    .type _psuser_run,@function
_psuser_run:
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    mov r15,r8
    mov r4,r9
    mov r5,r15
    mov #0,r10
    mov #127,r11
    add #1,r11
    mov #1,r12
    /* The SR transition itself runs privileged; r15 is now P0, but
       user instructions will only access the P0 arena. */
    stc sr,r0
    mov.l .Lsr_mask,r1
    and r1,r0
    mov.l .Lsr_irq,r1
    or r1,r0
    ldc r0,sr
    nop
.Luser_loop:
    /* User-mode stack accesses prove real P0 stack, not just TRAPA. */
    mov.l r10,@-r15
    mov.l @r15+,r13
    cmp/eq r10,r13
    bf .Luser_bad
    mov.l .Lquery,r4
    mov r10,r5
    .global _psuser_loop_trap
_psuser_loop_trap:
    trapa #0x7a
    cmp/eq r9,r0
    bf .Luser_bad
    add #1,r10
    cmp/eq r11,r10
    bf .Luser_loop
    bra .Luser_end
    nop
.Luser_bad:
    mov #0,r12
.Luser_end:
    mov.l .Lexit_service,r4
    mov r10,r5
    .global _psuser_exit_trap
_psuser_exit_trap:
    trapa #0x7a
    /* Ke only returns privileged for the exact exit trap after all
       128 services.  Restore P1 SP BEFORE any stack operation. */
    mov r8,r15
    mov r12,r0
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
.Lsr_mask: .long 0x8FFFFF0F
.Lsr_irq: .long 0x000000F0
.Lquery: .long 0x00001000
.Lexit_service: .long 0x000010FF
    .size _psuser_run,.-_psuser_run
