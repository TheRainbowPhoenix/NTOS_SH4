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
#ifndef NTOS_ENFORCE_PE_PAGE_PERMISSIONS
#define NTOS_ENFORCE_PE_PAGE_PERMISSIONS 0
#endif
#ifndef NTOS_ENABLE_CONTROLLED_VBR_FAULT_PROBE
#define NTOS_ENABLE_CONTROLLED_VBR_FAULT_PROBE 0
#endif
#ifndef NTOS_ENABLE_GUEST_PROTECT_DISPATCH
#define NTOS_ENABLE_GUEST_PROTECT_DISPATCH 0
#endif
#if NTOS_ENABLE_GUEST_PROTECT_DISPATCH && !NTOS_ENABLE_TLB_MISS_PAGER
#error "Guarded protection-exception dispatch requires RAM-backed VBR paging"
#endif
#if NTOS_ENABLE_CONTROLLED_VBR_FAULT_PROBE && !NTOS_ENABLE_TLB_MISS_PAGER
#error "The VBR canary requires the experimental TLB pager"
#endif
#ifndef NTOS_ENABLE_IRQ_OWNERSHIP
#define NTOS_ENABLE_IRQ_OWNERSHIP 0
#endif
#if NTOS_ENABLE_TLB_MISS_PAGER != NTOS_ENABLE_IRQ_OWNERSHIP
#error "Hardware paging requires both TLB miss and IRQ ownership gates"
#endif

#if NTOS_ENABLE_TLB_MISS_PAGER && NTOS_ENABLE_IRQ_OWNERSHIP
#include "fault_pager.hpp"
#include "nt/guest_exception.hpp"
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
    nt::GuestException guest_guard{};
    volatile uint32_t general_faults=0u;
    // A tiny, stackless P0 user-mode read probe returns to the original
    // privileged guest C frame using exactly one audited TRAPA #0x7B.
    // No OS/SDK code may run during the user-mode instruction window.
    bool user_read_session=false;
    uint32_t user_read_trapa_next=0u;
    uint32_t user_read_saved_sr=0u;
    volatile uint32_t user_read_returns=0u;
    host_world::Snapshot host{};
    uint8_t *vectors_raw=nullptr;
    uint8_t *vectors=nullptr;
    uint8_t *stack_raw=nullptr;
    bool installed=false;
    uint32_t fault_error=0u;
    uint32_t failed_address=0u;
    volatile uint32_t hw_faults=0u;
    volatile uint32_t hint_calls=0u;
    // One-shot expected unhinted fault canary. The canary EXE is
    // separately opt-in, never present in physical CP400 bundles.
    volatile uint32_t probe_page=0u;
    volatile uint32_t probe_event=0u;
    volatile uint32_t probe_seen=0u;
    volatile uint32_t probe_unexpected=0u;
    volatile bool probe_armed=false;
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
                               unsigned slot,unsigned read_only) {
    if(slot>=mmu::SLOT_ROTATING_LAST+1u || read_only>2u)return false;
    const uint32_t flags=read_only==2u ?
        mmu::PTEL_PAGE_SUPERVISOR_ONLY :
        read_only==1u ? mmu::PTEL_PAGE_READ_ONLY : mmu::PTEL_PAGE;
    if(!mmu::map_page_at_index_flags(va,pa,slot,flags))
        return false;
#if NTOS_VERIFY_UTLB_SLOTS
    return mmu::verify_slot(slot,va,pa,flags);
#else
    return true;
#endif
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
    if(ctx.probe_armed) {
        if((tea&~0xFFFu)==ctx.probe_page &&
           expevt==ctx.probe_event) {
            ctx.probe_seen=ctx.probe_seen+1u;
            ctx.probe_armed=false;
        } else ctx.probe_unexpected=ctx.probe_unexpected+1u;
    }
    return 1;
}

namespace ntos_hw {
// This callback reads only prevalidated P1-owned PE image metadata and
// cannot allocate, access an SDK file, or use the host LCD.
[[maybe_unused]] static bool executable_page(void *opaque,uint32_t pc) {
    if(pc&1u)return false;
    const auto &table=*static_cast<const ntos_fault::Pager *>(opaque);
    for(unsigned i=0u;i<table.modules_used;++i) {
        const auto &mod=table.modules[i];
        if(!mod.header || pc<mod.va ||
           (pc-mod.va)/0x1000u>=mod.pages)continue;
        return ntos_mm::pe_page_executable(
            *mod.header,(pc-mod.va)/0x1000u);
    }
    return false; // VM pages and API windows are never recovery code
}
inline nt::Status arm_guest_guard(uint32_t address,uint32_t event,
                                 uint32_t resume_pc) {
#if NTOS_ENABLE_GUEST_PROTECT_DISPATCH
    auto &c=g_context;
    if(!c.installed || !mmu::at_enabled())return nt::DEVICE_BUSY;
    const uint32_t page=address&~0xFFFu;
    if(c.table.lookup(page)<0)return nt::NOT_COMMITTED;
    // Only a pre-resident page with the expected hardware PR encoding
    // may be armed. Write: RO PTEL.PR=10. Read: DEBUG-ONLY supervisor
    // PTEL.PR=01 (a user P0 load must deliver EXPEVT 0x0A0).
    const ntos_mm::Access rights=c.table.access(page);
    if(event==nt::SH4_PROTECTION_WRITE ?
       rights!=ntos_mm::Access::read_only :
       event==nt::SH4_PROTECTION_READ ?
       rights!=ntos_mm::Access::privileged_only : true)
        return nt::INVALID_PARAMETER;
#if NTOS_VERIFY_UTLB_SLOTS
    // In the separately gated emulator candidate, trust the *actual* P4
    // UTLB data array rather than our software-page metadata. A failed
    // hardware PR readback must reject the canary BEFORE the bad access.
    const int slot=c.table.lookup(page);
    if(slot<0 || !mmu::verify_slot(
        static_cast<uint32_t>(slot),page,
        c.table.entries[slot].pa,
        rights==ntos_mm::Access::read_only ?
            mmu::PTEL_PAGE_READ_ONLY : mmu::PTEL_PAGE_SUPERVISOR_ONLY))
        return nt::DEVICE_BUSY;
#endif
    uint32_t sr=0u;
    __asm__ volatile("stc sr,%0" : "=r"(sr) :: "memory");
    const bool privileged=(sr&0x40000000u)!=0u;
    // All current NTOS entry points run as privileged foreground guests.
    // A supervisor can *read* PTEL.PR=01; only the narrow, stackless
    // P0 user transition can provoke a genuine 0x0A0 fault.
    if(event==nt::SH4_PROTECTION_READ) {
        if(!privileged || (sr&0x30000000u)!=0u ||
           c.user_read_session || resume_pc>0xFFFFFFFDu ||
           !executable_page(&c.table,resume_pc+2u))
            return nt::INVALID_PARAMETER;
    }
    const nt::Status status=c.guest_guard.arm(
        address,event,resume_pc,&executable_page,&c.table,
        event==nt::SH4_PROTECTION_WRITE && privileged);
    if(status==nt::SUCCESS && event==nt::SH4_PROTECTION_READ) {
        // resume_pc points at the single TRAPA #0x7B opcode inside
        // the independently compiled, stackless guest .text stub.
        // TRAPA stores SPC=opcode+2. On exit SSR restores host MD=1.
        c.user_read_saved_sr=sr;
        c.user_read_trapa_next=resume_pc+2u;
        c.user_read_session=true;
    }
    return status;
#else
    (void)address;(void)event;(void)resume_pc;
    return nt::NOT_SUPPORTED;
#endif
}
} // namespace ntos_hw

// VBR+0x100 general exception entry, independent of the +0x400 miss path.
// This callback can ONLY redirect a *pre-armed*, exact P0 protection fault
// onto an executable PE recovery label. Nonmatching exceptions return 0
// so the original host general vector receives them without RTE.
extern "C" int ntos_general_exception_callback(uint32_t tea,uint32_t event) {
#if NTOS_ENABLE_GUEST_PROTECT_DISPATCH
    auto &c=ntos_hw::g_context;
    uint32_t spc=0u,ssr=0u;
    __asm__ volatile("stc spc,%0" : "=r"(spc) :: "memory");
    __asm__ volatile("stc ssr,%0" : "=r"(ssr) :: "memory");
    // The narrow TRAPA return path must be checked before generic 0xA0/
    // 0xC0 dispatch. Never turn unrelated app traps into privileged RTEs.
    if(event==0x160u && c.user_read_session) {
        const uint32_t tra=*reinterpret_cast<volatile uint32_t *>(
            0xFF000020u);
        if(tra!=0x7Bu*4u || (ssr&0x40000000u)!=0u ||
           spc!=c.user_read_trapa_next ||
           !ntos_hw::executable_page(&c.table,spc))
            return 0;
        c.user_read_session=false;
        c.guest_guard.cancel();
        c.user_read_returns=c.user_read_returns+1u;
        // Restore the privileged guest's SR, including MD and IMASK.
        // The entry stub made NO stack accesses in user mode, so SGR
        // still contains the original (unchanged) privileged P1 SP.
        const uint32_t restored=c.user_read_saved_sr;
        __asm__ volatile("ldc %0,ssr" :: "r"(restored) : "memory");
        return 1;
    }
    if(event!=nt::SH4_PROTECTION_READ &&
       event!=nt::SH4_PROTECTION_WRITE)return 0;
    c.general_faults=c.general_faults+1u;
    c.last_tea=tea;c.last_expevt=event;
    c.last_spc=spc;c.last_ssr=ssr;c.last_mmucr=mmu::read_mmucr();
    uint32_t landing=0u;
    if(!c.guest_guard.decide(tea,event,spc,ssr,
                             &ntos_hw::executable_page,&c.table,
                             &landing))
        return 0;
    __asm__ volatile("ldc %0,spc" :: "r"(landing) : "memory");
    return 1;
#else
    (void)tea;(void)event;return 0;
#endif
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
    ctx.guest_guard.reset();
    ctx.general_faults=0u;
    ctx.user_read_session=false;
    ctx.user_read_trapa_next=0u;
    ctx.user_read_saved_sr=0u;
    ctx.user_read_returns=0u;
    ctx.probe_armed=false;
    ctx.probe_page=0u;
    ctx.probe_event=0u;
    ctx.probe_seen=0u;
    ctx.probe_unexpected=0u;
    ctx.last_tea=0u;
    ctx.last_expevt=0u;
    ctx.last_spc=0u;
    ctx.last_ssr=0u;
    ctx.last_mmucr=0u;
    Mem_Memset(&ctx.table,0,sizeof(ctx.table));
    return {&ctx,&prepare,&install,&verify,&uninstall};
}
inline bool add_module(uint32_t va,const uint8_t *backing,uint32_t pages,
                       const pe::Image *image=nullptr) {
    return g_context.table.add(va,mmu::phys_addr(backing),pages,
                               image,NTOS_ENFORCE_PE_PAGE_PERMISSIONS!=0);
}
inline uint32_t protection(uint32_t va) {
    return static_cast<uint32_t>(g_context.table.access(va));
}
inline int arm_probe(uint32_t va,uint32_t event) {
#if NTOS_ENABLE_CONTROLLED_VBR_FAULT_PROBE
    Context &ctx=g_context;
    if(!ctx.installed || !mmu::at_enabled() || ctx.probe_armed ||
       (event!=0x40u && event!=0x60u))return -1;
    const uint32_t page=va&~0xFFFu;
    if(ctx.table.lookup(page)>=0 ||
       ctx.table.access(page)!=ntos_mm::Access::read_write)
        return -2;
    uint32_t pa=0u;
    if(!ctx.table.backing(page,&pa))return -3;
    ctx.probe_page=page;
    ctx.probe_event=event;
    ctx.probe_seen=0u;
    ctx.probe_unexpected=0u;
    ctx.probe_armed=true;
    return 0;
#else
    (void)va;(void)event;
    return -1; // safe on physical device builds
#endif
}
inline bool prime(uint32_t va,bool pin=true) {
    return g_context.table.fault(va,&commit_translation,nullptr,pin);
}
} // namespace ntos_hw
#endif
