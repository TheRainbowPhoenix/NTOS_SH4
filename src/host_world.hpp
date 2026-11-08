// host_world.hpp -- Hollyhock stage0 CPU context fence inspired by gint.
//
// gint's world.c saves/restores driver + CPU state around a privileged
// world switch. NTOS currently retains Hollyhock device drivers/interrupt
// handlers while running PE modules, so it must NOT install gint's VBR
// handler or seize hardware IRQs. This is a reversible host CPU-state
// snapshot/restore for the P1-resident stage0, not a second gint kernel.
#pragma once
#include <stdint.h>
#if defined(__sh__)
#include "mmu.hpp"
#endif

namespace host_world {
struct Snapshot {
    uint32_t sr;
    uint32_t vbr;
    uint32_t gbr;
    uint32_t dbr; // UBC exception handler DBR (user supplied breakpoint demo)
    uint32_t mmucr;
    uint32_t pteh;
    uint32_t ptel;
};
inline Snapshot capture() {
    Snapshot s{};
#if defined(__sh__)
    __asm__ volatile("stc sr,%0" : "=r"(s.sr) :: "memory");
    __asm__ volatile("stc vbr,%0" : "=r"(s.vbr) :: "memory");
    __asm__ volatile("stc gbr,%0" : "=r"(s.gbr) :: "memory");
    __asm__ volatile("stc dbr,%0" : "=r"(s.dbr) :: "memory");
#endif
#if defined(__sh__)
    s.mmucr=mmu::read_mmucr();
    s.pteh=*reinterpret_cast<volatile uint32_t *>(mmu::REG_PTEH);
    s.ptel=*reinterpret_cast<volatile uint32_t *>(mmu::REG_PTEL);
#else
    s.mmucr=0u; // Host unit tests have no SH4 MMU.
#endif
    return s;
}
// Call only after leaving mapped P0 execution, disabling AT, and
// uninstalling NTOS's experimental VBR. Host AT must have been off at
// capture time (checked in run_mapped). Restore even if SR/VBR/GBR/DBR
// already match: NTOS changed MMUCR, PTEH, and PTEL during installation.
inline void restore(const Snapshot &s) {
#if defined(__sh__)
    uint32_t sr,vbr,gbr,dbr;
    __asm__ volatile("stc sr,%0" : "=r"(sr) :: "memory");
    __asm__ volatile("stc vbr,%0" : "=r"(vbr) :: "memory");
    __asm__ volatile("stc gbr,%0" : "=r"(gbr) :: "memory");
    __asm__ volatile("stc dbr,%0" : "=r"(dbr) :: "memory");
    const uint32_t mmucr=mmu::read_mmucr();
    const uint32_t pteh=
        *reinterpret_cast<volatile uint32_t *>(mmu::REG_PTEH);
    const uint32_t ptel=
        *reinterpret_cast<volatile uint32_t *>(mmu::REG_PTEL);
    if(sr==s.sr && vbr==s.vbr && gbr==s.gbr && dbr==s.dbr &&
       mmucr==s.mmucr && pteh==s.pteh && ptel==s.ptel)
        return;
    // Never restore a live host address-translation environment using this
    // primitive: reconstructing preexisting UTLB contents is not supported.
    if((s.mmucr&1u)!=0u)return;
    const uint32_t masked=sr|0x000000f0u; // maskable IRQs disabled
    __asm__ volatile("ldc %0,sr" :: "r"(masked) : "memory");
    // PTEH and PTEL are staging registers; neither write executes LDTLB.
    *reinterpret_cast<volatile uint32_t *>(mmu::REG_PTEH)=s.pteh;
    *reinterpret_cast<volatile uint32_t *>(mmu::REG_PTEL)=s.ptel;
    *reinterpret_cast<volatile uint32_t *>(mmu::REG_MMUCR)=s.mmucr;
    __asm__ volatile("ldc %0,vbr" :: "r"(s.vbr) : "memory");
    __asm__ volatile("ldc %0,gbr" :: "r"(s.gbr) : "memory");
    __asm__ volatile("ldc %0,dbr" :: "r"(s.dbr) : "memory");
    __asm__ volatile("ldc %0,sr" :: "r"(s.sr) : "memory");
#else
    (void)s;
#endif
}
} // namespace host_world
