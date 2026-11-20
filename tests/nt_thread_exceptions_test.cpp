// Pure host tests for bounded per-KTHREAD NT exception records.
#include "../src/nt/ke.hpp"
#include <cassert>
#include <cstdio>

static bool executable(void *,uint32_t pc) {
    return pc>=0x00401000u && pc<0x00402000u && !(pc&1u);
}
static bool revoked(void *,uint32_t pc) {
    return pc!=0x00401080u && executable(nullptr,pc);
}
int main() {
    nt::KernelContext k{};
    assert(k.begin(0x00400000u,0x20000u)==nt::SUCCESS);
    assert(k.enter_guest(0x00401000u)==nt::SUCCESS);
    constexpr uint32_t tea=0x06005000u,pc=0x00401020u;
    uint32_t a=17u,b=17u,landing=99u;
    assert(k.register_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0x00401080u,&executable,nullptr,&a)==nt::SUCCESS);
    assert(a!=0u && k.thread.exceptions.depth==1u);
    assert(k.register_exception(tea,0x040u,pc,0x00401080u,
        &executable,nullptr,&b)==nt::INVALID_PARAMETER && b==0u);
    assert(k.register_exception(tea,nt::KE_PROTECTION_WRITE,
        pc+1u,0x00401080u,&executable,nullptr,&b)==nt::INVALID_PARAMETER);
    assert(k.register_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0x8C000020u,&executable,nullptr,&b)==nt::INVALID_PARAMETER);
    assert(k.register_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,pc,&executable,nullptr,&b)==nt::INVALID_PARAMETER);
    assert(k.register_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0x00401082u,&executable,nullptr,&b)==nt::SUCCESS);
    assert(k.thread.exceptions.depth==2u && a!=b);
    assert(k.unregister_exception(a)==nt::INVALID_PARAMETER);
    assert(!k.dispatch_exception(tea+4u,nt::KE_PROTECTION_WRITE,
        pc,0u,&executable,nullptr,&landing) && landing==0u);
    assert(!k.dispatch_exception(tea,nt::KE_PROTECTION_READ,
        pc,0u,&executable,nullptr,&landing));
    assert(!k.dispatch_exception(tea,nt::KE_PROTECTION_WRITE,
        pc+2u,0u,&executable,nullptr,&landing));
    assert(!k.dispatch_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0x40010001u,&executable,nullptr,&landing));
    assert(k.thread.exceptions.depth==2u &&
           k.thread.exceptions.forwarded==4u);
    // Valid inner record is consumed once; outer remains registered.
    assert(k.dispatch_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0x00010001u,&executable,nullptr,&landing));
    assert(landing==0x00401082u && k.thread.exceptions.depth==1u);
    assert(k.thread.exceptions.last.thread_id==k.thread.unique_thread_id);
    assert(k.thread.exceptions.last.code==nt::ACCESS_VIOLATION);
    assert(k.thread.exceptions.last.fault_address==tea &&
           k.thread.exceptions.last.fault_pc==pc &&
           k.thread.exceptions.delivered==1u);
    assert(!k.dispatch_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0u,&revoked,nullptr,&landing) && landing==0u);
    assert(k.thread.exceptions.depth==1u);
    assert(k.dispatch_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0u,&executable,nullptr,&landing));
    assert(landing==0x00401080u && k.thread.exceptions.depth==0u);
    assert(!k.dispatch_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0u,&executable,nullptr,&landing));
    // Explicit removal; no fallback to arbitrary exception handlers.
    assert(k.register_exception(tea,nt::KE_PROTECTION_READ,
        pc,0x00401080u,&executable,nullptr,&a)==nt::SUCCESS);
    assert(k.unregister_exception(a)==nt::SUCCESS);
    assert(k.unregister_exception(a)==nt::INVALID_PARAMETER);
    // Unwind an inner record when an outer registration handles a fault.
    assert(k.register_exception(tea,nt::KE_PROTECTION_READ,
        pc,0x00401080u,&executable,nullptr,&a)==nt::SUCCESS);
    assert(k.register_exception(tea+4u,nt::KE_PROTECTION_WRITE,
        pc+2u,0x00401082u,&executable,nullptr,&b)==nt::SUCCESS);
    assert(k.dispatch_exception(tea,nt::KE_PROTECTION_READ,
        pc,0u,&executable,nullptr,&landing));
    assert(landing==0x00401080u && k.thread.exceptions.depth==0u);
    const uint32_t old_tid=k.thread.unique_thread_id;
    k.end();
    assert(k.thread.exceptions.depth==0u);
    assert(!k.dispatch_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0u,&executable,nullptr,&landing));
    assert(k.begin(0x00400000u,0x20000u)==nt::SUCCESS);
    assert(k.enter_guest(0x00401000u)==nt::SUCCESS);
    assert(k.thread.unique_thread_id!=old_tid);
    assert(k.thread.exceptions.depth==0u &&
           k.thread.exceptions.delivered==0u);
    nt::KernelContext other{};
    assert(other.begin(0x00400000u,0x20000u)==nt::SUCCESS);
    assert(other.enter_guest(0x00401000u)==nt::SUCCESS);
    assert(!other.dispatch_exception(tea,nt::KE_PROTECTION_WRITE,
        pc,0u,&executable,nullptr,&landing));
    k.end();other.end();
    std::puts("PASS KTHREAD exception records: LIFO, exact TEA/event/SPC/SSR, validated continuation, unwind, lifetime");
}
