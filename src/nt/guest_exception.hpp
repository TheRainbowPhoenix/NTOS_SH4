// Ps/Mm guarded SH4A guest protection exception dispatch.
//
// SH4A reports TLB read protection 0x0A0 and write protection 0x0C0 at
// VBR+0x100. This class is purely a one-shot decision engine. The P1 VBR
// callback is responsible for validating the original and recovery PCs,
// recording TEA/SPC/SSR, and writing a *validated* recovery PC into SPC.
// Never skip an arbitrary instruction, retry an illegal write, or call SDK.
// Unexpected exceptions must be FORWARDED to the previously saved OS vector.
//
// This is an opt-in emulator-first guest exception probe, NOT structured
// user-mode SEH, a general signal handler, or a universal process-killer.
#pragma once
#include <stdint.h>
#include "ntstatus.hpp"

namespace nt {
constexpr uint32_t SH4_PROTECTION_READ=0x0A0u;
constexpr uint32_t SH4_PROTECTION_WRITE=0x0C0u;
using GuestExecutable=bool (*)(void *,uint32_t);
struct GuestException {
    uint32_t target_page=0u;
    uint32_t event=0u;
    uint32_t recovery_pc=0u;
    uint32_t accepted=0u,forwarded=0u;
    uint32_t last_tea=0u,last_spc=0u,last_event=0u,last_status=0u;
    bool armed=false;
    Status arm(uint32_t va,uint32_t expected,uint32_t pc,
               GuestExecutable executable,void *context) {
        if(armed || (expected!=SH4_PROTECTION_READ &&
                     expected!=SH4_PROTECTION_WRITE) ||
           (pc&1u) || !pc || !executable ||
           !executable(context,pc))
            return INVALID_PARAMETER;
        target_page=va&~0xFFFu;
        event=expected;
        recovery_pc=pc;
        armed=true;
        last_status=0u;
        return SUCCESS;
    }
    // Incoming SSR.MD bit30 must be zero (interrupted P0 user mode).
    // No handling for privileged/host/IRQ-originated exceptions.
    bool decide(uint32_t tea,uint32_t code,uint32_t spc,uint32_t ssr,
                GuestExecutable executable,void *context,uint32_t *out_pc) {
        if(!out_pc)return false;
        *out_pc=0u;
        if(!armed || !executable ||
           (ssr&0x40000000u)!=0u || code!=event ||
           (tea&~0xFFFu)!=target_page ||
           (spc&1u)!=0u || !executable(context,spc) ||
           !executable(context,recovery_pc)) {
            ++forwarded;
            return false;
        }
        armed=false;
        last_tea=tea;last_event=code;last_spc=spc;
        last_status=ACCESS_VIOLATION;
        ++accepted;
        *out_pc=recovery_pc;
        return true;
    }
    void cancel() {armed=false;}
    void reset() {*this=GuestException{};}
};
} // namespace nt
