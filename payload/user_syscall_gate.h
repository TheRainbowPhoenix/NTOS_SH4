/* SH4A user->Ke gateway, single trap, no user stack operations.
 *
 * The WinCE PE/COREDLL ABI is left untouched: caller first arms a
 * one-shot Ke gate via NtosApi[54], then passes service in bank0 r4 and
 * argument in bank0 r5 to this entry. TRAPA #0x7A switches to P1 VBR,
 * the Ke dispatcher returns the result in bank0 r0, restores SR.MD=1
 * in SSR, and RTE resumes at RTS using the original PR and P1 stack.
 *
 * No MMU privileged instruction, heap, file, scheduler or SDK call
 * occurs while in user mode.
 */
#pragma once
#include <stdint.h>
__asm__(
    ".text\n"
    ".align 2\n"
    ".global _ntos_user_syscall_stub\n"
    "_ntos_user_syscall_stub:\n"
    "stc sr,r0\n"
    "mov.l 1f,r1\n"
    "and r1,r0\n"
    "mov.l 2f,r1\n"
    "or r1,r0\n"
    "ldc r0,sr\n"
    "nop\n"
    ".global _ntos_user_syscall_trapa\n"
    "_ntos_user_syscall_trapa:\n"
    "trapa #0x7a\n"
    "rts\n"
    "nop\n"
    ".align 2\n"
    "1: .long 0x8fffff0f\n" /* MD/RB/BL/IMASK cleared */
    "2: .long 0x000000f0\n" /* mask IRQ for one user instruction */
);
extern uint32_t ntos_user_syscall_stub(uint32_t service,uint32_t argument);
extern void ntos_user_syscall_trapa(void);
