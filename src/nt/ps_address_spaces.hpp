// Prototype dual-KPROCESS private address-space table. HOST preflight only.
// A real switch requires complete SH4AL-DSP frame switching, IRQ exclusion,
// P0 mapping ownership, and synchronous ITLB/UTLB invalidation.
// No kernel-owned static arenas; each KPROCESS gets its own external P1 VM.
#pragma once
#include <stdint.h>
#include "ke.hpp"
#include "address_space.hpp"
namespace nt {
using SpaceSwitchFlush=bool (*)(void *,uint32_t,uint32_t);
template<unsigned COUNT=2u>
struct ProcessSpaces {
    static_assert(COUNT>=2u && COUNT<=4u);
    struct Slot {
        KPROCESS process{};
        AddressSpaceRoot root{};
        bool live=false;
    };
    Slot slots[COUNT]{};
    uint32_t current_pid=0u;
    uint32_t last_pid=0x100u;

    const Slot *find(uint32_t pid) const {
        for(const auto &s:slots)
            if(s.live && s.process.unique_process_id==pid)return &s;
        return nullptr;
    }
    Status create(uint32_t parent,VirtualMemory<64,8> *vm,
                  uint32_t image_base,uint32_t image_size,uint32_t *out) {
        if(!out)return INVALID_PARAMETER;
        *out=0u;
        if(!vm || !vm->backing || !vm->physical || !image_base ||
           !image_size || (parent && !find(parent)) ||
           last_pid==0xFFFFFFFFu)
            return INVALID_PARAMETER;
        for(const auto &s:slots)if(s.live) {
            const uint32_t a=s.root.vm->physical,b=vm->physical;
            constexpr uint32_t extent=64u*4096u;
            if(a<=b ? b-a<extent : a-b<extent)
                return CONFLICTING_ADDRESSES;
        }
        Slot *target=nullptr;
        for(auto &s:slots)if(!s.live) {target=&s;break;}
        if(!target)return INSUFFICIENT_RESOURCES;
        const uint32_t pid=last_pid+1u;
        if(!target->root.prepare(pid,&current_pid,vm))
            return INVALID_PARAMETER;
        target->process.unique_process_id=pid;
        target->process.inherited_from_process_id=parent;
        target->process.image_base=image_base;
        target->process.image_bytes=image_size;
        target->process.threads=1u;
        target->process.active=true;
        target->live=true;
        last_pid=pid;
        *out=pid;
        return SUCCESS;
    }
    Status switch_to(uint32_t pid,SpaceSwitchFlush flush,void *context) {
        const Slot *target=find(pid);
        if(!target)return INVALID_PARAMETER;
        if(current_pid==pid)return SUCCESS;
        if(current_pid && (!flush ||
           !flush(context,current_pid,pid)))
            return DEVICE_BUSY;
        // Never publish the new PID until the prior ASID/TLB is gone.
        current_pid=pid;
        return SUCCESS;
    }
    bool resolve_current(uint32_t va,uint32_t *pa,ntos_mm::Access *rights)
        const {
        const Slot *s=find(current_pid);
        return s && s->root.resolve(va,pa,rights);
    }
};
} // namespace nt
