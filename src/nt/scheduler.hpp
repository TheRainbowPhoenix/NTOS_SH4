// Cooperative SH7305 SH4AL-DSP scheduler model, HOST-ONLY; no RTE switch yet.
// This CPU has NO FPU. Save integer/banked GPRs, SR/PR/GBR/MAC,
// DSP/loop control and address-space identity in the real assembly path.
#pragma once
#include <stdint.h>
#include "ntstatus.hpp"
namespace nt {
struct Sh4AlDsp {
    uint32_t a[2]{},ag[2]{},x[2]{},y[2]{},m[2]{};
    uint32_t dsr=0u,mod=0u,rs=0u,re=0u;
};
struct SH4Context {
    uint32_t gpr[16]{},bank0[8]{},bank1[8]{};
    uint32_t sr=0u,ssr=0u,spc=0u,sgr=0u,pr=0u;
    uint32_t gbr=0u,mach=0u,macl=0u;
    Sh4AlDsp dsp{};
    bool complete=false;
};
struct DispatchSlot {
    uint32_t pid=0u,tid=0u;
    SH4Context context{};
    bool ready=false,exited=false;
};
template<unsigned COUNT=2u>
struct CooperativeScheduler {
    static_assert(COUNT>=2u && COUNT<=4u);
    DispatchSlot slots[COUNT]{};
    unsigned active=COUNT;
    Status register_thread(unsigned index,uint32_t pid,uint32_t tid,
                           const SH4Context &context) {
        if(index>=COUNT || !pid || !tid || !context.complete ||
           slots[index].tid)return INVALID_PARAMETER;
        for(const auto &s:slots)if(s.tid==tid)return INVALID_PARAMETER;
        slots[index]={pid,tid,context,true,false};
        return SUCCESS;
    }
    Status begin(unsigned index) {
        if(active!=COUNT || index>=COUNT || !slots[index].ready ||
           slots[index].exited)return INVALID_PARAMETER;
        active=index;return SUCCESS;
    }
    // Host-only decision; hardware context switch must be separate.
    Status yield(const SH4Context &saved,unsigned *next,
                 SH4Context *restore) {
        if(!next || !restore || active>=COUNT || !saved.complete)
            return INVALID_PARAMETER;
        unsigned choice=COUNT;
        for(unsigned n=1u;n<=COUNT;++n) {
            const unsigned j=(active+n)%COUNT;
            if(slots[j].ready && !slots[j].exited) {
                choice=j;break;
            }
        }
        if(choice==COUNT)return DEVICE_BUSY;
        slots[active].context=saved;
        *restore=slots[choice].context;*next=choice;
        active=choice;return SUCCESS;
    }
    Status stop(unsigned index) {
        if(index>=COUNT || !slots[index].tid)return INVALID_PARAMETER;
        slots[index].exited=true;slots[index].ready=false;
        if(active==index)active=COUNT;
        return SUCCESS;
    }
};
} // namespace nt
