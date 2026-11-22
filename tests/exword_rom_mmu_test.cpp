#include "../exword/rom32a_mmu.hpp"
#include <cassert>
#include <cstdio>
using namespace exword_rom32a;
int main() {
    /* Matches MMU_HandleTranslationFault and MMU_ConfigureAddinMappings
     * from XD-A8500 ROM32A.BIN.c; no device MMU accesses in this test. */
    Windows w{0x70000000u,0x70100000u,0x0c700000u,
              0x74000000u,0x74020000u,0x0c800000u};
    assert(valid(w));
    Mapping a=resolve(0x70001007u,w);
    assert(a.zone==Zone::addin_primary && a.physical==0x0c701007u);
    assert(a.ptel_flags==0x15cu);
    Mapping b=resolve(0x74001ffcu,w);
    assert(b.zone==Zone::addin_api && b.physical==0x0c801ffcu);
    assert(b.ptel_flags==0x17cu);
    auto f=resolve(0x08100000u,w);
    assert(f.zone==Zone::firmware_alias);
    assert(f.physical==0x10100000u);
    assert(f.ptel_flags==0x17cu);
    assert(!resolve(0x73ffffffu,w).valid());
    assert(!resolve(0x75ffffffu,w).valid());
    assert(!resolve(0x78000000u,w).valid());
    assert(!resolve(0x80000000u,w).valid());
    assert(!valid({0x70000000u,0x74100000u,0x0c700000u,
                   0x74000000u,0x74020000u,0x0c800000u}));
    assert(guest_owned(0x8c700800u,4096u,
                       0x8c700000u,0x8c7f0000u,0x8c7f8000u,0x8c800000u));
    assert(!guest_owned(0x8c7effffu,4096u,
                        0x8c700000u,0x8c7f0000u,0x8c7f8000u,0x8c800000u));
    assert(guest_hint(0x8c700000u,0x8c700000u,0x8c7f0000u,
                      0x8c7f8000u,0x8c800000u)==0);
    assert(guest_hint(0x70000000u,0x8c700000u,0x8c7f0000u,
                      0x8c7f8000u,0x8c800000u)==-2);
    assert(guest_hint(0x08100000u,0x8c700000u,0x8c7f0000u,
                      0x8c7f8000u,0x8c800000u)==-2);
    assert(MMUCR_DISABLE==0x104u && MMUCR_ENABLE==0x101u);
    puts("PASS XD-A8500 ROM32A translation windows, PTEL rights and owned P1 hint");
}
