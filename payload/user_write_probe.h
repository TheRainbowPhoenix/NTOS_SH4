/* One-store SH4BE user-mode write-protection probe.
 * Mirrors the REAL CP400-verified read probe's SR.MD transition, exactly.
 * Never touches the stack, SDK, heap, or filesystem in user mode.
 * Arm EXPEVT=0x0C0 for the exact RO byte target FIRST; recovery is the
 * in-function TRAPA #0x7b which only the private P1 VBR accepts.
 */
#pragma once
#include <stdint.h>
__asm__(
    ".text\n"
    ".align 2\n"
    ".global _ntos_user_write_probe_stub\n"
    "_ntos_user_write_probe_stub:\n"
    "stc sr,r0\n"
    "mov.l 1f,r1\n"
    "and r1,r0\n"
    "mov.l 2f,r1\n"
    "or r1,r0\n"
    "ldc r0,sr\n"
    "nop\n"
    "mov #0x37,r0\n"  /* test value, must NEVER reach RO page */
    ".global _ntos_user_write_probe_store\n"
    "_ntos_user_write_probe_store:\n"
    "mov.b r0,@r4\n" /* exactly one user-mode protected write */
    ".global _ntos_user_write_probe_trapa\n"
    "_ntos_user_write_probe_trapa:\n"
    "trapa #0x7b\n"
    "rts\n"
    "nop\n"
    ".align 2\n"
    "1: .long 0x8fffff0f\n" /* clear MD/RB/BL/IMASK */
    "2: .long 0x000000f0\n" /* mask IRQs */
);
extern void ntos_user_write_probe_stub(uint32_t target);
extern void ntos_user_write_probe_trapa(void);
extern void ntos_user_write_probe_store(void);
