// Ke two-KTHREAD C-ABI cooperative bridge. Only privileged foreground
// SH4AL-DSP threads in one KPROCESS. NO user-mode preemption, DSP live
// switch, ASID handoff, SDK calls, or runnable second PE.
#pragma once
#include <stdint.h>
#include "ke.hpp"
namespace nt {
struct CoopSaved {
    uint32_t r8_r14[7]{};
    uint32_t sp=0u,pr=0u,gbr=0u,mach=0u,macl=0u,sr=0u;
    // SH4AL-DSP, NOT FPU: A0,A1,A0G,A1G,M0,M1,X0,X1,Y0,Y1,
    // DSR,RS,RE,MOD. Populated by the architectural MOVS/STS/STC ops.
    uint32_t dsp[14]{};
};
static_assert(sizeof(CoopSaved)==108u);
static_assert(__builtin_offsetof(CoopSaved,sp)==28u);
static_assert(__builtin_offsetof(CoopSaved,pr)==32u);
static_assert(__builtin_offsetof(CoopSaved,sr)==48u);
static_assert(__builtin_offsetof(CoopSaved,dsp)==52u);

struct KeCoop {
    CoopSaved frames[2]{};
    KTHREAD peer{};
    uint32_t owner_pid=0u,first_tid=0u;
    uint32_t switches=0u,active=0u;
    bool created=false,exited=false;
    void reset() {*this=KeCoop{};}
    Status create(const KernelContext &ke,uint32_t entry,uint32_t stack,
                  uint32_t sr,uint32_t gbr,uint32_t *out_tid) {
        if(!out_tid)return INVALID_PARAMETER;
        *out_tid=0u;
        if(created || !ke.running() || !entry || !stack ||
           (entry&1u) || (stack&15u) ||
           // Require the SH4AL-DSP register bank enabled while the
           // paired DSP context is saved/restored in assembly.
           (sr&0x70001000u)!=0x40001000u ||
           ke.thread.unique_thread_id>=0xFFFFFFFEu)
            return INVALID_PARAMETER;
        owner_pid=ke.process.unique_process_id;
        first_tid=ke.thread.unique_thread_id;
        peer.owning_process_id=owner_pid;
        peer.unique_thread_id=first_tid+1u;
        peer.start_address=entry;
        peer.state=KTHREAD_STATE::ready;
        frames[1].sp=stack;
        frames[1].pr=entry;
        frames[1].gbr=gbr;
        frames[1].sr=sr;
        created=true;
        *out_tid=peer.unique_thread_id;
        return SUCCESS;
    }
    bool next(const KernelContext &ke,CoopSaved **from,
              const CoopSaved **to) {
        if(!created || exited || !ke.running() || !from || !to ||
           ke.process.unique_process_id!=owner_pid)
            return false;
        const unsigned origin=active;
        const unsigned target=origin^1u;
        *from=&frames[origin];
        *to=&frames[target];
        if(target==1u && peer.state==KTHREAD_STATE::ready)
            peer.state=KTHREAD_STATE::running;
        active=target;
        ++switches;
        return true;
    }
    bool exit_peer(const KernelContext &ke,CoopSaved **from,
                   const CoopSaved **to) {
        if(!created || exited || !ke.running() || active!=1u ||
           ke.process.unique_process_id!=owner_pid)
            return false;
        peer.state=KTHREAD_STATE::terminated;
        exited=true;active=0u;++switches;
        *from=&frames[1];*to=&frames[0];
        return true;
    }
    uint32_t tid(const KernelContext &ke) const {
        return ke.running() && ke.process.unique_process_id==owner_pid &&
               created && active==1u ? peer.unique_thread_id :
               ke.running()?ke.thread.unique_thread_id:0u;
    }
};
} // namespace nt
