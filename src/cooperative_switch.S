/* C-ABI cooperative Ke stack switch — SH7305 SH4AL-DSP, NO FPU.
 * Saves r8-r15, PR, GBR, MACH/MACL and SR with IRQ masked.
 * Not a full asynchronous exception frame. r0-r7 are C caller-saved;
 * SR.DSP=1 is rejected until DSP state can be saved in hardware.
 * r4=from 52-byte context, r5=to 52-byte context.
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
