/* Call a relocatable SH4BE NTOS PE from its separate, vetted P1 stack.
 * r4 entry, r5 API table, r6 guest stack end. Return int in r0.
 * Preserve host PR/GBR/VBR/SR/MAC/callee registers even on normal return.
 * A faulting guest is NOT recoverable without a dedicated guest VBR.
 */
.text
.global _ntos_exword_invoke
_ntos_exword_invoke:
    sts.l pr,@-r15
    stc.l gbr,@-r15
    stc.l sr,@-r15
    stc.l vbr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    mov r15,r8
    mov r4,r0
    mov r5,r4
    mov r6,r15
    mov #0,r5
    mov #0,r6
    jsr @r0
    nop
    mov r8,r15
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,vbr
    ldc.l @r15+,sr
    ldc.l @r15+,gbr
    lds.l @r15+,pr
    rts
    nop
