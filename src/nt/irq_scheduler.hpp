// Ke bounded timer IRQ scheduling policy. HOST PREFLIGHT ONLY.
// Actual SH7305 INTEVT/P1 exception context capture and timer ownership
// belong to HAL; no code here programs a timer or steals the HH3 VBR.
// Full SH4AL-DSP context is passed by the hardware assembly owner.
#pragma once
#include <stdint.h>
#include "scheduler.hpp"
namespace nt {
template<unsigned COUNT=2u>
struct IrqScheduler {
    CooperativeScheduler<COUNT> ke{};
    uint32_t expected_event=0u;
    uint32_t quantum=1u,remaining=1u;
    uint32_t total_irqs=0u,dispatches=0u,rejected=0u;
    bool armed=false,nested=false;
    Status arm(uint32_t event,uint32_t slice) {
        if(armed || !event || !slice || slice>1024u)
            return INVALID_PARAMETER;
        expected_event=event;quantum=remaining=slice;
        armed=true;return SUCCESS;
    }
    Status timer_irq(uint32_t intevt,const SH4Context &interrupted,
                     unsigned *next,SH4Context *resume,
                     BeforeProcessSwitch flush,void *opaque) {
        if(!armed || nested || intevt!=expected_event ||
           !next || !resume || !interrupted.complete ||
           // Future handler must deliver a genuine unprivileged P0
           // SSR/SPC/SGR and complete integer+DSP save, no FPU.
           (interrupted.ssr&0x70000000u)!=0u ||
           (interrupted.spc&1u) || !interrupted.spc ||
           interrupted.spc>=0x80000000u ||
           (interrupted.sgr&3u) || !interrupted.sgr ||
           interrupted.sgr>=0x80000000u ||
           ke.active>=COUNT) {
            ++rejected;return INVALID_PARAMETER;
        }
        // Never touch HAL/SDK/heap here; this is a bounded policy object.
        nested=true;
        ++total_irqs;
        if(remaining>1u) {
            --remaining;
            *next=ke.active;
            *resume=interrupted;
            nested=false;
            return SUCCESS;
        }
        unsigned next_slot=COUNT;
        SH4Context replacement{};
        const Status result=ke.yield(interrupted,&next_slot,&replacement,
                                      flush,opaque);
        if(result==SUCCESS) {
            *next=next_slot;*resume=replacement;
            remaining=quantum;++dispatches;
        }
        // A refused P4 root ack cannot publish a new KTHREAD or PID.
        nested=false;
        return result;
    }
};
} // namespace nt
