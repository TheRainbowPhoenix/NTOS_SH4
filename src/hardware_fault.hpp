// hardware_fault.hpp -- experimental SH7305 VBR ownership + RAM-backed
// UTLB miss service. Only compiled when BOTH feature gates are enabled.
//
// A physical SH4A CPU takes TLB misses at VBR+0x400 (EXPEVT 0x40/0x60,
// faulting address in TEA); general exceptions at +0x100, IRQ at +0x600.
// The assembly vectors are installed in aligned P1 RAM, with general/IRQ
// forwarding and a dedicated P1 exception stack. No host filesystem,
// heap, LCD or SDK functions are called from the exception path.
//
// We intentionally do NOT claim this first-stage handler is device-proven.
// It is opt-in and should be tested on an emulator before real hardware.
#pragma once
#ifndef NTOS_ENABLE_TLB_MISS_PAGER
#define NTOS_ENABLE_TLB_MISS_PAGER 0
#endif
#ifndef NTOS_ENABLE_IRQ_OWNERSHIP
#define NTOS_ENABLE_IRQ_OWNERSHIP 0
#endif
#if NTOS_ENABLE_TLB_MISS_PAGER != NTOS_ENABLE_IRQ_OWNERSHIP
#error "Hardware paging requires both TLB miss and IRQ ownership gates"
#endif

#if NTOS_ENABLE_TLB_MISS_PAGER && NTOS_ENABLE_IRQ_OWNERSHIP
#include "fault_pager.hpp"
#include "irq_handoff.hpp"

extern "C" {
    // Literal references embedded into the copied VBR assembly template.
    uint32_t ntos_saved_general_vector=0u;
    uint32_t ntos_saved_tlb_vector=0u;
    uint32_t ntos_saved_irq_vector=0u;
    uint32_t ntos_exception_stack_top=0u;
    extern uint8_t ntos_vbr_template_start[];
    extern uint8_t ntos_vbr_template_end[];
}
namespace ntos_hw {
struct Context {
    ntos_fault::Pager table{};
    host_world::Snapshot host{};
    uint8_t *vectors_raw=nullptr;
    uint8_t *vectors=nullptr;
    uint8_t *stack_raw=nullptr;
    bool installed=false;
    uint32_t fault_error=0u;
    uint32_t failed_address=0u;
    volatile uint32_t hw_faults=0u;
    volatile uint32_t hint_calls=0u;
    // Only written inside the P1-resident exception callback. Read and
    // printed after VBR returns to Hollyhock, never via SDK in the ISR.
    volatile uint32_t last_tea=0u;
    volatile uint32_t last_expevt=0u;
    volatile uint32_t last_spc=0u;
    volatile uint32_t last_ssr=0u;
    volatile uint32_t last_mmucr=0u;
};
static Context g_context{};

static bool commit_translation(void *,uint32_t va,uint32_t pa,
                               unsigned slot,unsigned way) {
    if(slot>=mmu::SLOT_ROTATING_LAST+1u || way>3u)return false;
    mmu::map_page_at_index(va,pa,slot,way);
    return true;
}
} // namespace ntos_hw

extern "C" int ntos_pager_exception_callback(uint32_t tea,uint32_t expevt) {
    // Called only from the assembly VBR miss vector on the P1 fault stack.
    // Host syscall, File_Read, Mem_Malloc, printf, exceptions forbidden.
    auto &ctx=ntos_hw::g_context;
    uint32_t spc=0u,ssr=0u;
    __asm__ volatile("stc spc,%0" : "=r"(spc) :: "memory");
    __asm__ volatile("stc ssr,%0" : "=r"(ssr) :: "memory");
    // Snapshot first; if the callback ever runs, its interrupted PC must
    // be visible even when the pager rejects the fault and chains to OS.
    ctx.last_tea=tea;
    ctx.last_expevt=expevt;
    ctx.last_spc=spc;
    ctx.last_ssr=ssr;
    ctx.last_mmucr=mmu::read_mmucr();
    ctx.hw_faults=ctx.hw_faults+1u;
    if(expevt!=0x40u && expevt!=0x60u) {
        ++ctx.fault_error;
        ctx.failed_address=tea;
        return 0;
    }
    if(!ctx.table.fault(tea,&ntos_hw::commit_translation,nullptr,false,true)) {
        ++ctx.fault_error;
        ctx.failed_address=tea;
        return 0;
    }
    return 1;
}

namespace ntos_hw {
inline uint32_t read_sr() {
    uint32_t v;
    __asm__ volatile("stc sr,%0" : "=r"(v) :: "memory");
    return v;
}
inline uint32_t read_vbr() {
    uint32_t v;
    __asm__ volatile("stc vbr,%0" : "=r"(v) :: "memory");
    return v;
}
inline void update_vbr(uint32_t newvbr) {
    const uint32_t old=read_sr();
    const uint32_t blocked=old|0x000000f0u;
    __asm__ volatile("ldc %0,sr" :: "r"(blocked) : "memory");
    __asm__ volatile("ldc %0,vbr" :: "r"(newvbr) : "memory");
    __asm__ volatile("ldc %0,sr" :: "r"(old) : "memory");
}
static bool prepare(void *opaque) {
    Context &ctx=*static_cast<Context *>(opaque);
    const uint32_t size=static_cast<uint32_t>(
        ntos_vbr_template_end-ntos_vbr_template_start);
    if(size!=0x800u || ctx.vectors_raw || ctx.stack_raw)
        return false;
    ctx.vectors_raw=static_cast<uint8_t *>(Mem_Malloc(0x1000u+0xFFFu));
    ctx.stack_raw=static_cast<uint8_t *>(Mem_Malloc(0x2000u+0xFFFu));
    if(!ctx.vectors_raw || !ctx.stack_raw) {
        if(ctx.vectors_raw)Mem_Free(ctx.vectors_raw);
        if(ctx.stack_raw)Mem_Free(ctx.stack_raw);
        ctx.vectors_raw=nullptr;ctx.stack_raw=nullptr;
        return false;
    }
    ctx.vectors=static_cast<uint8_t *>(align_page(ctx.vectors_raw));
    uint8_t *stack=static_cast<uint8_t *>(align_page(ctx.stack_raw));
    ntos_exception_stack_top=reinterpret_cast<uint32_t>(stack+0x2000u);
    ntos_saved_general_vector=ctx.host.vbr+0x100u;
    ntos_saved_tlb_vector=ctx.host.vbr+0x400u;
    ntos_saved_irq_vector=ctx.host.vbr+0x600u;
    Mem_Memcpy(ctx.vectors,ntos_vbr_template_start,size);
    Mem_Memset(stack,0,0x2000u);
    // The copied vector contains dirty D-cache lines. SH4 OCBr
    // invalidation WITHOUT writeback could silently discard the handler.
    // Flush with ocbwb+icbi, mirroring our proven ELF/PE image staging.
    flush_image(ctx.vectors,size);
    flush_image(stack,0x2000u);
    return true;
}
static bool install(void *opaque) {
    Context &ctx=*static_cast<Context *>(opaque);
    if(!ctx.vectors || ctx.installed)return false;
    update_vbr(reinterpret_cast<uint32_t>(ctx.vectors));
    ctx.installed=true;
    return true;
}
static bool verify(void *opaque) {
    Context &ctx=*static_cast<Context *>(opaque);
    return ctx.vectors &&
           read_vbr()==reinterpret_cast<uint32_t>(ctx.vectors);
}
static void uninstall(void *opaque) {
    Context &ctx=*static_cast<Context *>(opaque);
    if(ctx.installed) {
        update_vbr(ctx.host.vbr);
        ctx.installed=false;
    }
    if(ctx.vectors_raw) Mem_Free(ctx.vectors_raw);
    if(ctx.stack_raw) Mem_Free(ctx.stack_raw);
    ctx.vectors_raw=nullptr;ctx.vectors=nullptr;ctx.stack_raw=nullptr;
    ntos_exception_stack_top=0u;
}
inline ntos_irq::Hooks hooks(const host_world::Snapshot &host) {
    Context &ctx=g_context;
    ctx.host=host;
    ctx.fault_error=0u;
    ctx.failed_address=0u;
    ctx.hw_faults=0u;
    ctx.hint_calls=0u;
    ctx.last_tea=0u;
    ctx.last_expevt=0u;
    ctx.last_spc=0u;
    ctx.last_ssr=0u;
    ctx.last_mmucr=0u;
    Mem_Memset(&ctx.table,0,sizeof(ctx.table));
    return {&ctx,&prepare,&install,&verify,&uninstall};
}
inline bool add_module(uint32_t va,const uint8_t *backing,uint32_t pages) {
    return g_context.table.add(va,mmu::phys_addr(backing),pages);
}
inline bool prime(uint32_t va,bool pin=true) {
    return g_context.table.fault(va,&commit_translation,nullptr,pin);
}
} // namespace ntos_hw
#endif
