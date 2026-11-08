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
constexpr unsigned SETS=32u;
constexpr unsigned WAYS=4u;
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
    uint8_t set;
    uint8_t way;
    bool present;
    bool pinned;
};
using Install=bool (*)(void *,uint32_t,uint32_t,unsigned,unsigned);
struct Pager {
    Area modules[MAX_MODULES]{};
    Entry entries[SLOTS]{};
    uint8_t reserved_ways[SETS]{}; // MMU API window and host-pinned slots
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
    bool reserve_way(unsigned set,unsigned way) {
        if(set>=SETS || way>=WAYS)return false;
        uint8_t bit=static_cast<uint8_t>(1u<<way);
        if(reserved_ways[set]&bit)return false;
        for(unsigned i=0;i<SLOTS;++i)
            if(entries[i].present && entries[i].set==set &&
               entries[i].way==way)return false;
        reserved_ways[set]|=bit;
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
    // A given VPN can appear in at most one slot. A write fault does not
    // demand a page copy because the RAM backing frame persists.
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
            // Software metadata is not proof of current hardware residency.
            // A genuine TLB miss on a tracked VPN must reinstall its old
            // translation. Explicit hints, however, are cheap cache hits.
            if(hardware_miss &&
               !install(ctx,e.va,e.pa,static_cast<unsigned>(existing),e.way)) {
                servicing=false;
                return false;
            }
            e.age=++clock;
            if(pin)e.pinned=true;
            servicing=false;
            return true;
        }
        const unsigned set=(vpn>>12u)&(SETS-1u);
        uint8_t ways=reserved_ways[set];
        unsigned occupancy=0u;
        for(unsigned w=0;w<WAYS;++w)
            if(ways&(1u<<w))++occupancy;
        for(unsigned i=0;i<SLOTS;++i) {
            const Entry &e=entries[i];
            if(e.present && e.set==set) {
                ways|=static_cast<uint8_t>(1u<<e.way);
                ++occupancy;
            }
        }
        // If this VPN's set already occupies four ways, replace one
        // unpinned way *in that set*. Otherwise pick a free global slot,
        // or evict an old slot in any set.
        int victim=-1;
        if(occupancy>=WAYS) {
            for(unsigned i=0;i<SLOTS;++i) {
                const Entry &e=entries[i];
                if(!e.present || e.set!=set || e.pinned)continue;
                if(victim<0 || e.age<entries[victim].age)
                    victim=static_cast<int>(i);
            }
        } else {
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
        }
        if(victim<0){servicing=false;return false;}
        Entry &out=entries[static_cast<unsigned>(victim)];
        const bool occupied=out.present;
        unsigned way=0u;
        if(occupied && out.set==set)
            way=out.way;
        else {
            while(way<WAYS && (ways&(1u<<way))) ++way;
            if(way>=WAYS){servicing=false;return false;}
        }
        // Install callback must update the SH7305 UTLB slot synchronously.
        // Publish software metadata only AFTER the hardware install works.
        if(!install(ctx,vpn,pa,static_cast<unsigned>(victim),way)) {
            servicing=false;return false;
        }
        if(occupied)++evictions;
        out={vpn,pa,++clock,static_cast<uint8_t>(set),
             static_cast<uint8_t>(way),true,pin};
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
