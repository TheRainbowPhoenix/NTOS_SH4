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
    static constexpr unsigned IMAGE_RANGES=8u;
    struct ImageRange {
        uint32_t va=0u,pa=0u,pages=0u;
        ntos_mm::Access rights=ntos_mm::Access::no_access;
        bool shared=false;
    };
    struct Slot {
        KPROCESS process{};
        AddressSpaceRoot root{};
        ImageRange images[IMAGE_RANGES]{};
        unsigned image_count=0u;
        bool live=false;
    };
    static bool overlaps(uint32_t a,uint32_t a_pages,
                         uint32_t b,uint32_t b_pages) {
        const uint32_t a_end=a+a_pages*VM_PAGE;
        const uint32_t b_end=b+b_pages*VM_PAGE;
        return a<b_end && b<a_end;
    }
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
            // Newly allocated VM frames may not alias an existing
            // process's loaded PE text/data physical backing.
            for(unsigned i=0u;i<s.image_count;++i)
                if(overlaps(vm->physical,64u,
                            s.images[i].pa,s.images[i].pages))
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
    // Prepare one resident PE section range of this KPROCESS. Multiple
    // ranges represent PE .text (RO) and .data/.bss (RW) separately.
    // SH4A has no hardware NX: this only tracks read/write rights.
    // Shared read-only COREDLL backing must explicitly match VA, PA,
    // length and permissions on BOTH roots. All private image PAs must
    // be disjoint, even when the processes use identical virtual VAs.
    // Do not install this into live P4 translation arrays here.
    Status add_image(uint32_t pid,uint32_t va,uint32_t pa,
                     uint32_t count,ntos_mm::Access rights,
                     bool shared=false) {
        // After the first guest root becomes current, any live PE
        // mapping change requires a separate hardware TLB transaction.
        if(current_pid)return DEVICE_BUSY;
        Slot *owner=nullptr;
        for(auto &slot:slots)
            if(slot.live && slot.process.unique_process_id==pid)
                owner=&slot;
        if(!owner || owner->image_count>=IMAGE_RANGES ||
           !count || !va || !pa || (va&(VM_PAGE-1u)) ||
           (pa&(VM_PAGE-1u)) ||
           va>=0x80000000u || count>(0x80000000u-va)/VM_PAGE ||
           pa>=0x20000000u || count>(0x20000000u-pa)/VM_PAGE ||
           rights==ntos_mm::Access::no_access ||
           rights==ntos_mm::Access::privileged_only ||
           (shared && rights!=ntos_mm::Access::read_only) ||
           // Protect VM arena and the privileged API window.
           overlaps(va,count,VM_BASE,(VM_END-VM_BASE)/VM_PAGE) ||
           overlaps(va,count,0x08100000u,4u))
            return INVALID_PARAMETER;
        for(const auto &s:slots)if(s.live) {
            if(overlaps(pa,count,s.root.vm->physical,64u))
                return CONFLICTING_ADDRESSES;
            for(unsigned i=0u;i<s.image_count;++i) {
                const ImageRange &old=s.images[i];
                if(&s==owner &&
                   overlaps(va,count,old.va,old.pages))
                    return CONFLICTING_ADDRESSES;
                if(overlaps(pa,count,old.pa,old.pages)) {
                    // No partial shared-PA aliases: the RO module must
                    // be exactly the same mapping in both processes.
                    if(&s==owner || !shared || !old.shared ||
                       rights!=old.rights ||
                       va!=old.va || pa!=old.pa || count!=old.pages)
                        return CONFLICTING_ADDRESSES;
                }
            }
        }
        // Reserve no mutable hardware state until ALL checks pass.
        owner->images[owner->image_count++]={va,pa,count,rights,shared};
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
        if(!s || !s->root.valid() || !pa || !rights)return false;
        const uint32_t aligned=va&~(VM_PAGE-1u);
        for(unsigned i=0u;i<s->image_count;++i) {
            const ImageRange &m=s->images[i];
            if(aligned>=m.va &&
               (aligned-m.va)/VM_PAGE<m.pages) {
                *pa=m.pa+(aligned-m.va);
                *rights=m.rights;
                return true;
            }
        }
        return s->root.resolve(va,pa,rights);
    }
};
} // namespace nt
