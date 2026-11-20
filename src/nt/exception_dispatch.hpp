// Bounded, thread-owned NT-style exception registration and dispatch.
//
// Phase 1: only exact, pre-registered SH4A user-mode protection faults.
// No guest-linked-list dereference, arbitrary handler callbacks, SEH/TEB,
// host SDK, allocation, or scheduler activity in the P1 VBR callback.
// A successful dispatch supplies a separately validated recovery PC to HAL.
// All other exceptions MUST chain to the previous host VBR handler.
#pragma once
#include <stdint.h>
#include "ntstatus.hpp"

namespace nt {
using ThreadExecutable=bool (*)(void *,uint32_t);
constexpr uint32_t KE_PROTECTION_READ=0x0A0u;
constexpr uint32_t KE_PROTECTION_WRITE=0x0C0u;
constexpr unsigned KE_EXCEPTION_DEPTH=4u;

struct KEXCEPTION_RECORD {
    uint32_t thread_id=0u;
    Status code=0u;
    uint32_t event=0u;
    uint32_t fault_address=0u;
    uint32_t fault_pc=0u;
    uint32_t continuation=0u;
    uint32_t saved_ssr=0u;
};
struct KEXCEPTION_REGISTRATION {
    uint32_t token=0u;
    uint32_t thread_id=0u;
    uint32_t event=0u;
    uint32_t fault_address=0u;
    uint32_t fault_pc=0u;
    uint32_t continuation=0u;
};
struct ThreadExceptions {
    KEXCEPTION_REGISTRATION registrations[KE_EXCEPTION_DEPTH]{};
    KEXCEPTION_RECORD last{};
    uint32_t next_token=1u;
    uint32_t delivered=0u,forwarded=0u;
    unsigned depth=0u;
    bool dispatching=false;

    static bool protection(uint32_t event) {
        return event==KE_PROTECTION_READ || event==KE_PROTECTION_WRITE;
    }
    Status push(uint32_t thread_id,uint32_t tea,uint32_t event,
                uint32_t fault_pc,uint32_t continuation,
                ThreadExecutable executable,void *context,uint32_t *token) {
        if(!token)return INVALID_PARAMETER;
        *token=0u;
        // Explicit P0 guest instruction and continuation, not P1/P4/host.
        if(dispatching || !thread_id || !tea || tea>=0x80000000u ||
           !protection(event) || !fault_pc || !continuation ||
           fault_pc>=0x80000000u || continuation>=0x80000000u ||
           (fault_pc&1u) || (continuation&1u) ||
           fault_pc==continuation || !executable ||
           !executable(context,fault_pc) ||
           !executable(context,continuation))
            return INVALID_PARAMETER;
        if(depth==KE_EXCEPTION_DEPTH || !next_token)
            return INSUFFICIENT_RESOURCES;
        const uint32_t id=next_token++;
        registrations[depth++]={id,thread_id,event,tea,fault_pc,
                                 continuation};
        *token=id;
        return SUCCESS;
    }
    Status pop(uint32_t thread_id,uint32_t token) {
        // Removal is LIFO. A stale token from a different KTHREAD is
        // never allowed to remove a new thread's registration.
        if(dispatching || !depth || !token ||
           registrations[depth-1u].thread_id!=thread_id ||
           registrations[depth-1u].token!=token)
            return INVALID_PARAMETER;
        registrations[--depth]={};
        return SUCCESS;
    }
    bool dispatch(uint32_t thread_id,uint32_t tea,uint32_t event,
                  uint32_t spc,uint32_t ssr,ThreadExecutable executable,
                  void *context,uint32_t *landing) {
        if(!landing)return false;
        *landing=0u;
        if(dispatching || !thread_id || !protection(event) ||
           (ssr&0x70000000u)!=0u || !executable) {
            ++forwarded;
            return false;
        }
        dispatching=true;
        for(unsigned n=depth;n!=0u;--n) {
            const unsigned ix=n-1u;
            const KEXCEPTION_REGISTRATION &reg=registrations[ix];
            if(reg.thread_id!=thread_id || reg.fault_address!=tea ||
               reg.event!=event || reg.fault_pc!=spc)
                continue;
            // Revalidate BOTH PCs on delivery. A modified module layout
            // or wrong guest origin cannot redirect the supervisor SPC.
            if(!executable(context,spc) ||
               !executable(context,reg.continuation))break;
            const uint32_t resume=reg.continuation;
            last={thread_id,ACCESS_VIOLATION,event,tea,spc,resume,ssr};
            // Consuming an outer registration unwinds every inner record.
            for(unsigned j=ix;j<depth;++j)registrations[j]={};
            depth=ix;
            ++delivered;
            *landing=resume;
            dispatching=false;
            return true;
        }
        dispatching=false;
        ++forwarded;
        return false;
    }
    void reset() {*this=ThreadExceptions{};}
};
} // namespace nt
