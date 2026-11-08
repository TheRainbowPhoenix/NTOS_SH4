/* NTOS SH7305 experimental VBR: generalized exception+IRQ forwarding
 * and RAM-backed UTLB miss. Must be explicitly compiled with BOTH
 * NTOS_ENABLE_TLB_MISS_PAGER=1 and NTOS_ENABLE_IRQ_OWNERSHIP=1.
 *
 * The 0x100 and 0x600 stubs preserve r0 in the branch delay slot
 * and jump to the original host handler. The 0x400 stub switches to
 * a dedicated P1 stack BEFORE any push, saves the entire kernel
 * register bank plus PR/MACH/MACL/GBR, calls a no-SDK C++ pager,
 * then returns with RTE (or chains to the original host TLB handler).
 *
 * Untested on a physical CP400: never activate in the default build.
 */
#if NTOS_ENABLE_TLB_MISS_PAGER && NTOS_ENABLE_IRQ_OWNERSHIP
    .section .ntos.vbr,"ax"
    .align 2
    .global _ntos_vbr_template_start
    .global _ntos_vbr_template_end
_ntos_vbr_template_start:
    .space 0x100,0

    .org 0x100
.Lgeneral:
    mov.l r0,@-r15
    mov.l .Lold_general_var,r0
    mov.l @r0,r0
    jmp @r0
    mov.l @r15+,r0
    .align 2
.Lold_general_var:
    .long _ntos_saved_general_vector

    .org 0x400
.Lmiss:
    /* SGR retains the interrupted SP; put original r0 in the old
       r15 register until we can save it on a new P1 exception stack. */
    mov r0,r15
    mov.l .Lfault_stack_var,r0
    mov.l @r0,r0
    mov.l r15,@-r0
    mov r0,r15
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.l r3,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
    mov.l r6,@-r15
    mov.l r7,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    sts.l pr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l gbr,@-r15

    mov.l .Ltea_reg,r0
    mov.l @r0,r4
    mov.l .Lexpevt_reg,r0
    mov.l @r0,r5
    mov.l .Lfault_callback,r0
    jsr @r0
    nop
    /* Callback returns 1 for mapped fault, 0 for invalid P0 address.
       If invalid, restore registers and chain to the old OS TLB handler. */
    tst r0,r0
    bf .Lmiss_ok
    mov #1,r0
    bra .Lmiss_restore
    nop
.Lmiss_ok:
    mov #0,r0
.Lmiss_restore:
    /* Keep failure flag in banked r1, which is restored after dispatch.
       Do not touch SSR/SPC: RTE will restart the faulting instruction. */
    mov r0,r1
    ldc.l @r15+,gbr
    lds.l @r15+,macl
    lds.l @r15+,mach
    lds.l @r15+,pr
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    mov.l @r15+,r7
    mov.l @r15+,r6
    mov.l @r15+,r5
    mov.l @r15+,r4
    mov.l @r15+,r3
    mov.l @r15+,r2
    /* r1 is the saved interrupted register again; failure flag is
       held in T bit using tst before restoring the saved r1. */
    tst r1,r1
    mov.l @r15+,r1
    mov.l @r15+,r0
    bt .Lmiss_return
    /* Failed: forward to the original host TLB-miss handler. */
    stc sgr,r15
    mov.l r0,@-r15
    mov.l .Lold_tlb_var,r0
    mov.l @r0,r0
    jmp @r0
    mov.l @r15+,r0
.Lmiss_return:
    stc sgr,r15
    rte
    nop

    .align 2
.Lfault_stack_var: .long _ntos_exception_stack_top
.Ltea_reg:         .long 0xFF00000C
.Lexpevt_reg:      .long 0xFF000024
.Lfault_callback:  .long _ntos_pager_exception_callback
.Lold_tlb_var:     .long _ntos_saved_tlb_vector

    .org 0x600
.Lirq:
    mov.l r0,@-r15
    mov.l .Lold_irq_var,r0
    mov.l @r0,r0
    jmp @r0
    mov.l @r15+,r0
    .align 2
.Lold_irq_var:
    .long _ntos_saved_irq_vector

    .org 0x800
_ntos_vbr_template_end:
#endif
