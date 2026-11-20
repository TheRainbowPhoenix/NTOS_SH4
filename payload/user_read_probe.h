/* One-instruction SH4A user-mode read probe: intentionally minimal.
 *
 * This header emits two global SH4BE symbols when included in ONE
 * self-contained probe EXE (MMREAD or MMRDINIT). It executes NO SDK calls,
 * has NO stack accesses between lowering SR.MD and TRAPA #0x7b, and
 * masks interrupts during the transient user-mode window.
 *
 * Before invoking, the privileged loader must successfully arm the exact
 * 0x0A0 exception and register _ntos_user_probe_trapa as its recovery PC.
 * The gated VBR+0x100 trap returns from TRAPA to the following RTS with
 * the original privileged SR restored. Never use this outside the probe.
 */
#pragma once
#include <stdint.h>
__asm__(
    ".text\n"
    ".align 2\n"
    ".global _ntos_user_probe_stub\n"
    "_ntos_user_probe_stub:\n"
    "stc sr,r0\n"
    "mov.l 1f,r1\n"
    "and r1,r0\n"      /* clear SR.MD, SR.RB and SR.BL */
    "mov.l 2f,r1\n"
    "or r1,r0\n"       /* block maskable interrupts: IMASK=15 */
    "ldc r0,sr\n"
    "nop\n"
    "mov.b @r4,r0\n"   /* user-mode protected read, TEA exactly r4 */
    ".global _ntos_user_probe_trapa\n"
    "_ntos_user_probe_trapa:\n"
    "trapa #0x7b\n"   /* CPU saves SPC=trapa_pc+2 and SSR=user SR */
    "rts\n"           /* executed only after VBR restores MD=1 */
    "nop\n"
    ".align 2\n"
    "1: .long 0x8fffff0f\n" /* clear MD/RB/BL/IMASK */
    "2: .long 0x000000f0\n"
);
extern void ntos_user_probe_stub(uint32_t address);
extern void ntos_user_probe_trapa(void);
