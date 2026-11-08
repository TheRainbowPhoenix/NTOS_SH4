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
    uint32_t dbr; // UBC exception handler DBR (user supplied breakpoint demo)
    uint32_t mmucr;
};
inline Snapshot capture() {
    Snapshot s{};
#if defined(__sh__)
    __asm__ volatile("stc sr,%0" : "=r"(s.sr) :: "memory");
    __asm__ volatile("stc vbr,%0" : "=r"(s.vbr) :: "memory");
    __asm__ volatile("stc gbr,%0" : "=r"(s.gbr) :: "memory");
    __asm__ volatile("stc dbr,%0" : "=r"(s.dbr) :: "memory");
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
    uint32_t sr,vbr,gbr,dbr;
    __asm__ volatile("stc sr,%0" : "=r"(sr) :: "memory");
    __asm__ volatile("stc vbr,%0" : "=r"(vbr) :: "memory");
    __asm__ volatile("stc gbr,%0" : "=r"(gbr) :: "memory");
    __asm__ volatile("stc dbr,%0" : "=r"(dbr) :: "memory");
    // No changes in the normal P1/host-driver path. Avoid gratuitous
    // writes to SR and VBR; only a payload that altered CPU world
    // registers requires the exceptional repair path.
    if(sr==s.sr && vbr==s.vbr && gbr==s.gbr && dbr==s.dbr)
        return;
    const uint32_t masked=sr|0x000000f0u; // SR.IMASK=15
    __asm__ volatile("ldc %0,sr" :: "r"(masked) : "memory");
    __asm__ volatile("ldc %0,vbr" :: "r"(s.vbr) : "memory");
    __asm__ volatile("ldc %0,gbr" :: "r"(s.gbr) : "memory");
    __asm__ volatile("ldc %0,dbr" :: "r"(s.dbr) : "memory");
    __asm__ volatile("ldc %0,sr" :: "r"(s.sr) : "memory");
#else
    (void)s;
#endif
}
} // namespace host_world
