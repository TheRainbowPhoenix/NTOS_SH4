// host_world.hpp -- Hollyhock stage0 CPU context fence inspired by gint.
//
// gint's world.c saves/restores driver + CPU state around a privileged
// world switch. NTOS currently retains Hollyhock device drivers/interrupt
// handlers while running PE modules, so it must NOT install gint's VBR
// handler or seize hardware IRQs. This is a reversible host CPU-state
// snapshot/restore for the P1-resident stage0, not a second gint kernel.
#pragma once
#include <stdint.h>
#include "mmu.hpp"

namespace host_world {
struct Snapshot {
    uint32_t sr;
    uint32_t vbr;
    uint32_t gbr;
    uint32_t mmucr;
};
inline Snapshot capture() {
    Snapshot s{};
#if defined(__sh__)
    __asm__ volatile("stc sr,%0" : "=r"(s.sr) :: "memory");
    __asm__ volatile("stc vbr,%0" : "=r"(s.vbr) :: "memory");
    __asm__ volatile("stc gbr,%0" : "=r"(s.gbr) :: "memory");
#endif
    s.mmucr=mmu::read_mmucr();
    return s;
}
// Call only after exiting mapped P0 execution and disabling the added TLB
// entries. Temporarily mask maskable interrupts while restoring VBR/GBR;
// the original SR (including original IMASK and bank flags) goes back last.
// We do not restore AT=1 or host TLB contents: NTOS only takes this path
// from the HH3 execution environment with AT=0.
inline void restore(const Snapshot &s) {
#if defined(__sh__)
    uint32_t masked;
    __asm__ volatile("stc sr,%0" : "=r"(masked) :: "memory");
    masked |= 0x000000f0u; // SH4 SR.IMASK=15, temporarily mask IRQs
    __asm__ volatile("ldc %0,sr" :: "r"(masked) : "memory");
    __asm__ volatile("ldc %0,vbr" :: "r"(s.vbr) : "memory");
    __asm__ volatile("ldc %0,gbr" :: "r"(s.gbr) : "memory");
    __asm__ volatile("ldc %0,sr" :: "r"(s.sr) : "memory");
#else
    (void)s;
#endif
}
} // namespace host_world
