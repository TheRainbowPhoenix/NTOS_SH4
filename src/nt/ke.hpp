// NTOS Ke/Ps foreground context (phase 1).
//
// Keep the NT shape: KPROCESS owns address-space/dispatch identity;
// KTHREAD owns a saved trap frame, run state and system-service accounting.
// Ps resource objects (Ob handles, Ex pool, private VM, WinCE DLL graph)
// remain in Process. HAL owns SR/SSR/SPC/VBR and TRAPA entry, not Ke.
// One foreground thread only: NO claim of multitasking or preemption.
// All functions are allocation-free, deterministic and SDK/FS-independent.
#pragma once
#include <stdint.h>
#include "ntstatus.hpp"
#include "exception_dispatch.hpp"

namespace nt {
constexpr uint32_t SH4A_SYSTEM_SERVICE_EVENT=0x160u;
constexpr uint32_t SH4A_SYSTEM_SERVICE_TRAPA=0x7Au;
constexpr uint32_t SH4A_SYSTEM_SERVICE_TRA=SH4A_SYSTEM_SERVICE_TRAPA*4u;
constexpr uint32_t SERVICE_QUERY_PROCESS_ID=0x1000u;
constexpr uint32_t SERVICE_QUERY_THREAD_ID=0x1001u;
constexpr uint32_t SERVICE_YIELD_EXECUTION=0x1002u;

enum class KTHREAD_STATE : uint8_t {
    initialized=0u,ready=1u,running=2u,terminated=3u
};
struct KPROCESS {
    uint32_t unique_process_id=0u;
    uint32_t image_base=0u;
    uint32_t image_bytes=0u;
    uint32_t threads=0u;
    bool active=false;
};
struct KTRAP_FRAME {
    uint32_t spc=0u,ssr=0u,sgr=0u;
    uint32_t service=0u,argument=0u,return_value=0u;
    uint32_t exception_event=0u,tra=0u;
};
struct KTHREAD {
    uint32_t unique_thread_id=0u;
    uint32_t owning_process_id=0u;
    uint32_t start_address=0u;
    KTHREAD_STATE state=KTHREAD_STATE::initialized;
    KTRAP_FRAME last_trap{};
    ThreadExceptions exceptions{};
    uint32_t syscall_count=0u;
    uint32_t voluntary_yields=0u;
    uint32_t rejected_traps=0u;
};

struct KernelContext {
    KPROCESS process{};
    KTHREAD thread{};
    uint32_t generation=0u;
    bool in_service=false;

    Status begin(uint32_t image_base,uint32_t image_bytes) {
        if(process.active || thread.state==KTHREAD_STATE::running ||
           !image_base || !image_bytes || generation==0xffffffffu)
            return INVALID_PARAMETER;
        const uint32_t next=generation+1u;
        *this=KernelContext{};
        generation=next;
        process.unique_process_id=0x100u+next;
        process.image_base=image_base;
        process.image_bytes=image_bytes;
        process.threads=1u;
        process.active=true;
        thread.owning_process_id=process.unique_process_id;
        thread.unique_thread_id=0x10000u+next;
        thread.start_address=image_base;
        thread.state=KTHREAD_STATE::ready;
        return SUCCESS;
    }
    Status enter_guest(uint32_t entry) {
        if(!process.active || thread.state!=KTHREAD_STATE::ready ||
           entry<process.image_base ||
           entry-process.image_base>=process.image_bytes)
            return INVALID_PARAMETER;
        thread.start_address=entry;
        thread.state=KTHREAD_STATE::running;
        return SUCCESS;
    }
    bool running() const {
        return process.active &&
               thread.state==KTHREAD_STATE::running && !in_service;
    }
    void end() {
        in_service=false;
        thread.exceptions.reset(); // no stale fault landing PC after exit
        if(process.active)thread.state=KTHREAD_STATE::terminated;
        process.active=false;
        process.threads=0u;
    }

    // Phase 1: kernel-owned, bounded records. There is deliberately no
    // guest registration API until a dedicated one-op CP400 canary passes.
    Status register_exception(uint32_t tea,uint32_t event,uint32_t fault_pc,
                              uint32_t continuation,ThreadExecutable executable,
                              void *context,uint32_t *token) {
        if(!running())return DEVICE_BUSY;
        return thread.exceptions.push(thread.unique_thread_id,tea,event,
            fault_pc,continuation,executable,context,token);
    }
    Status unregister_exception(uint32_t token) {
        if(!running())return DEVICE_BUSY;
        return thread.exceptions.pop(thread.unique_thread_id,token);
    }
    bool dispatch_exception(uint32_t tea,uint32_t event,uint32_t spc,
                            uint32_t ssr,ThreadExecutable executable,
                            void *context,uint32_t *landing) {
        return running() && thread.exceptions.dispatch(
            thread.unique_thread_id,tea,event,spc,ssr,executable,
            context,landing);
    }

    static bool valid_service(uint32_t id) {
        return id==SERVICE_QUERY_PROCESS_ID ||
               id==SERVICE_QUERY_THREAD_ID ||
               id==SERVICE_YIELD_EXECUTION;
    }

    // A single trap is handled with P1-only data. No SDK, allocator, P0
    // memory load/store, blocked wait, thread switch or scheduler tick.
    Status dispatch(uint32_t spc,uint32_t ssr,uint32_t sgr,uint32_t tra,
                    uint32_t service,uint32_t arg,uint32_t *result) {
        if(!result || !running() || tra!=SH4A_SYSTEM_SERVICE_TRA ||
           (ssr&0x70000000u)!=0u || !valid_service(service)) {
            ++thread.rejected_traps;
            return INVALID_PARAMETER;
        }
        in_service=true;
        KTRAP_FRAME frame{};
        frame.spc=spc;frame.ssr=ssr;frame.sgr=sgr;
        frame.tra=tra;frame.exception_event=SH4A_SYSTEM_SERVICE_EVENT;
        frame.service=service;frame.argument=arg;
        switch(service) {
            case SERVICE_QUERY_PROCESS_ID:
                frame.return_value=process.unique_process_id;break;
            case SERVICE_QUERY_THREAD_ID:
                frame.return_value=thread.unique_thread_id;break;
            case SERVICE_YIELD_EXECUTION:
                // Cooperative dispatch point only. No runnable peer yet.
                ++thread.voluntary_yields;
                frame.return_value=SUCCESS;break;
            default:
                in_service=false;return INVALID_PARAMETER;
        }
        thread.last_trap=frame;
        ++thread.syscall_count;
        *result=frame.return_value;
        in_service=false;
        return SUCCESS;
    }
};

// Distinct from WinCE COREDLL's userspace API. A fault whose event/TRA,
// saved user SR, expected post-trap PC, or live Ke state does not match
// MUST go to the previous host handler, never be treated as a service.
inline bool user_service_trap_allowed(const KernelContext &kernel,
    bool armed,uint32_t expevt,uint32_t tra,uint32_t spc,uint32_t ssr,
    uint32_t expected_next,uint32_t saved_supervisor_sr,
    uint32_t expected_service,uint32_t actual_service) {
    return kernel.running() && armed &&
           expevt==SH4A_SYSTEM_SERVICE_EVENT &&
           tra==SH4A_SYSTEM_SERVICE_TRA &&
           (ssr&0x70000000u)==0u &&
           (saved_supervisor_sr&0x70000000u)==0x40000000u &&
           spc==expected_next && !(spc&1u) &&
           KernelContext::valid_service(expected_service) &&
           expected_service==actual_service;
}
} // namespace nt
