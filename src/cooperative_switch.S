/* C-ABI cooperative Ke stack switch — SH7305 SH4AL-DSP, NO FPU.
 * Saves r8-r15, PR, GBR, MACH/MACL, SR and all 14 SH4AL-DSP
 * data/control words. No FR/FPSCR/FPUL instructions exist on SH7305.
 * r0-r7 are C caller-saved: not a full interrupt/preemption frame.
 * r4=from 108-byte context, r5=to 108-byte context.
 */
    .section .text
    .align 2
    .global _ntos_ke_coop_swap
    .type _ntos_ke_coop_swap,@function
_ntos_ke_coop_swap:
    stc sr,r0
    mov.l r0,@(48,r4)
    or #0xF0,r0
    ldc r0,sr
    nop
    mov.l r8,@(0,r4)
    mov.l r9,@(4,r4)
    mov.l r10,@(8,r4)
    mov.l r11,@(12,r4)
    mov.l r12,@(16,r4)
    mov.l r13,@(20,r4)
    mov.l r14,@(24,r4)
    mov.l r15,@(28,r4)
    sts pr,r0
    mov.l r0,@(32,r4)
    stc gbr,r0
    mov.l r0,@(36,r4)
    sts mach,r0
    mov.l r0,@(40,r4)
    sts macl,r0
    mov.l r0,@(44,r4)

    ! DSP save. r2=&from.dsp[14]; store in reverse so memory order
    ! matches A0,A1,A0G,A1G,M0,M1,X0,X1,Y0,Y1,DSR,RS,RE,MOD.
    mov r4,r2
    add #108,r2
    stc.l mod,@-r2
    stc.l re,@-r2
    stc.l rs,@-r2
    sts.l dsr,@-r2
    movs.l y1,@-r2
    movs.l y0,@-r2
    movs.l x1,@-r2
    movs.l x0,@-r2
    movs.l m1,@-r2
    movs.l m0,@-r2
    movs.l a1g,@-r2
    movs.l a0g,@-r2
    movs.l a1,@-r2
    movs.l a0,@-r2

    ! DSP restore from target thread before resuming its saved SR.
    mov r5,r2
    add #52,r2
    movs.l @r2+,a0
    movs.l @r2+,a1
    movs.l @r2+,a0g
    movs.l @r2+,a1g
    movs.l @r2+,m0
    movs.l @r2+,m1
    movs.l @r2+,x0
    movs.l @r2+,x1
    movs.l @r2+,y0
    movs.l @r2+,y1
    lds.l @r2+,dsr
    ldc.l @r2+,rs
    ldc.l @r2+,re
    ldc.l @r2+,mod

    mov.l @(0,r5),r8
    mov.l @(4,r5),r9
    mov.l @(8,r5),r10
    mov.l @(12,r5),r11
    mov.l @(16,r5),r12
    mov.l @(20,r5),r13
    mov.l @(24,r5),r14
    mov.l @(28,r5),r15
    mov.l @(32,r5),r0
    lds r0,pr
    mov.l @(36,r5),r0
    ldc r0,gbr
    mov.l @(40,r5),r0
    lds r0,mach
    mov.l @(44,r5),r0
    lds r0,macl
    mov.l @(48,r5),r0
    ldc r0,sr
    nop
    rts
    nop
    .size _ntos_ke_coop_swap, .-_ntos_ke_coop_swap
