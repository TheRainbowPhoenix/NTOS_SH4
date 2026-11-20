// Kernel boundary for NT Native in/out argument words.
// A PE caller's raw uint32_t* is never trusted: validate a P0 VA against
// the current process's *resident writable* page policy before dereference.
// Cross-page, P1/P2/P4 host, null, unaligned or unmapped pointers fail.
#pragma once
#include <stdint.h>
namespace nt {
using WritableGuestPage=bool (*)(void *,uint32_t);
inline bool guest_writable_word(uintptr_t address,
                               WritableGuestPage lookup,void *context) {
    if(address<0x1000u || address>=0x80000000u ||
       (address&3u)!=0u || (address&0xFFFu)>0xFFCu || !lookup)
        return false;
    return lookup(context,static_cast<uint32_t>(address));
}
} // namespace nt
