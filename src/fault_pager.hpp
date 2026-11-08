// fault_pager.hpp -- SH7305 P0 virtual-page -> P1-backed physical-page
// fault service. No heap, filesystem, SDK or LCD calls in the hot path.
//
// The module image has already been validated, relocated and imported in
// P1 heap memory before AT is enabled. A real UTLB miss only needs to map
// the missing virtual page to its existing physical frame. All writes
// persist in that backing frame, even if its TLB translation is evicted.
//
// This bounded pager is CPU independent and testable. Enabling hardware
// faults requires VBR +0x400 entry trampoline and atomic ownership transfer
// under NTOS_ENABLE_TLB_MISS_PAGER (disabled by default).
#pragma once
#include <stdint.h>
#include "pe_rebase.hpp"

namespace ntos_fault {
constexpr unsigned MAX_MODULES=16u;
constexpr unsigned SLOTS=55u;
constexpr uint32_t PAGE=4096u;
struct Area {
    uint32_t va;
    uint32_t pages;
    uint32_t pa;  // physical page aligned, image's P1 heap backing
};
struct Entry {
    uint32_t va;
    uint32_t pa;
    uint32_t age;
    bool present;
    bool pinned;
};
// The SH7305 has a 64-entry fully associative UTLB, not the SH7705's
// 32 sets x 4 ways. Slots 0..54 are NTOS-owned; 55..58 map the API window.
// The final callback argument is reserved for ABI compatibility and is 0.
using Install=bool (*)(void *,uint32_t,uint32_t,unsigned,unsigned);
struct Pager {
    Area modules[MAX_MODULES]{};
    Entry entries[SLOTS]{};
    unsigned modules_used=0u;
    uint32_t clock=0u,misses=0u,evictions=0u;
    bool servicing=false;

    bool add(uint32_t va,uint32_t pa,uint32_t pages) {
        if(modules_used>=MAX_MODULES || !pages ||
           (va&(PAGE-1u)) || (pa&(PAGE-1u)) ||
           va<ntos_rebase::VA_MIN || va>=ntos_rebase::VA_MAX ||
           pages>(ntos_rebase::VA_MAX-va)/PAGE)
            return false;
        for(unsigned i=0;i<modules_used;++i)
            if(ntos_rebase::overlap(va,pages,
                modules[i].va,modules[i].pages))
                return false;
        modules[modules_used++]={va,pages,pa};
        return true;
    }
    bool backing(uint32_t va,uint32_t *pa) const {
        if(!pa)return false;
        const uint32_t aligned=va&~(PAGE-1u);
        for(unsigned i=0;i<modules_used;++i) {
            const Area &a=modules[i];
            if(aligned>=a.va && (aligned-a.va)/PAGE<a.pages) {
                const uint32_t offset=aligned-a.va;
                if(a.pa>0xffffffffu-offset) return false;
                *pa=a.pa+offset;
                return true;
            }
        }
        return false;
    }
    int lookup(uint32_t va) const {
        va&=~(PAGE-1u);
        for(unsigned i=0;i<SLOTS;++i)
            if(entries[i].present && entries[i].va==va)
                return static_cast<int>(i);
        return -1;
    }
    bool fault(uint32_t va,Install install,void *ctx,bool pin=false,
               bool hardware_miss=false) {
        if(servicing || !install)return false;
        servicing=true;
        uint32_t pa=0;
        const uint32_t vpn=va&~(PAGE-1u);
        if(!backing(vpn,&pa)) {servicing=false;return false;}
        const int existing=lookup(vpn);
        if(existing>=0) {
            Entry &e=entries[static_cast<unsigned>(existing)];
            // ITLB and UTLB residency are independent. An actual miss
            // on a tracked page still requires LDTLB, even if NTOS
            // previously installed that mapping.
            if(hardware_miss &&
               !install(ctx,e.va,e.pa,static_cast<unsigned>(existing),0u)) {
                servicing=false;return false;
            }
            e.age=++clock;
            if(pin)e.pinned=true;
            servicing=false;
            return true;
        }
        // Fully associative replacement: an empty NTOS-owned slot first,
        // otherwise the oldest unpinned page anywhere in the 55 slots.
        int victim=-1;
        for(unsigned i=0;i<SLOTS;++i)
            if(!entries[i].present) {
                victim=static_cast<int>(i);break;
            }
        if(victim<0)
            for(unsigned i=0;i<SLOTS;++i) {
                const Entry &e=entries[i];
                if(e.pinned)continue;
                if(victim<0 || e.age<entries[victim].age)
                    victim=static_cast<int>(i);
            }
        if(victim<0){servicing=false;return false;}
        Entry &out=entries[static_cast<unsigned>(victim)];
        if(!install(ctx,vpn,pa,static_cast<unsigned>(victim),0u)) {
            servicing=false;return false;
        }
        if(out.present)++evictions;
        out={vpn,pa,++clock,true,pin};
        ++misses;
        servicing=false;
        return true;
    }
};
inline bool choose_virtual(uint32_t preferred,uint32_t pages,
                           const ntos_rebase::Region *used,unsigned count,
                           uint32_t *out) {
    if(!out || !used || count>MAX_MODULES || !pages ||
       pages>(ntos_rebase::VA_MAX-ntos_rebase::VA_MIN)/PAGE)
        return false;
    auto valid=[&](uint32_t v) {
        if((v&(PAGE-1u)) || v<ntos_rebase::VA_MIN ||
           v>=ntos_rebase::VA_MAX ||
           pages>(ntos_rebase::VA_MAX-v)/PAGE)
            return false;
        for(unsigned i=0;i<count;++i)
            if(ntos_rebase::overlap(v,pages,used[i].base,used[i].pages))
                return false;
        return true;
    };
    if(valid(preferred)){*out=preferred;return true;}
    for(uint32_t v=ntos_rebase::FALLBACK;
        v<ntos_rebase::VA_MAX;v+=PAGE)
        if(valid(v)){*out=v;return true;}
    return false;
}
} // namespace ntos_fault
