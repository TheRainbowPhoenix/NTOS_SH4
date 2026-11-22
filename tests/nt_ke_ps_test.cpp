#include "../src/nt/ke.hpp"
#include "../src/nt/process.hpp"
#include <cassert>
#include <cstdio>
int main() {
    nt::Process ps{};
    assert(ps.begin());
    auto &k=ps.ke;
    assert(k.begin(0x00400000u,0x10000u)==nt::SUCCESS);
    assert(k.process.active && k.process.threads==1u);
    assert(k.thread.state==nt::KTHREAD_STATE::ready);
    const uint32_t pid=k.process.unique_process_id;
    const uint32_t tid=k.thread.unique_thread_id;
    assert(k.enter_guest(0x003FFFFFu)==nt::INVALID_PARAMETER);
    assert(k.enter_guest(0x00410000u)==nt::INVALID_PARAMETER);
    assert(k.enter_guest(0x00400200u)==nt::SUCCESS);
    assert(k.running());
    assert(k.enter_guest(0x00400200u)==nt::INVALID_PARAMETER);
    constexpr uint32_t next=0x00401200u;
    constexpr uint32_t saved=0x40010001u;
    constexpr uint32_t u=0x000000F0u;
    assert(nt::user_service_trap_allowed(
        k,true,0x160u,0x1E8u,next,u,next,saved,
        nt::SERVICE_QUERY_THREAD_ID,nt::SERVICE_QUERY_THREAD_ID));
    assert(!nt::user_service_trap_allowed(
        k,false,0x160u,0x1E8u,next,u,next,saved,
        nt::SERVICE_QUERY_THREAD_ID,nt::SERVICE_QUERY_THREAD_ID));
    assert(!nt::user_service_trap_allowed(
        k,true,0x0C0u,0x1E8u,next,u,next,saved,
        nt::SERVICE_QUERY_THREAD_ID,nt::SERVICE_QUERY_THREAD_ID));
    assert(!nt::user_service_trap_allowed(
        k,true,0x160u,0x1ECu,next,u,next,saved,
        nt::SERVICE_QUERY_THREAD_ID,nt::SERVICE_QUERY_THREAD_ID));
    assert(!nt::user_service_trap_allowed(
        k,true,0x160u,0x1E8u,next+2u,u,next,saved,
        nt::SERVICE_QUERY_THREAD_ID,nt::SERVICE_QUERY_THREAD_ID));
    assert(!nt::user_service_trap_allowed(
        k,true,0x160u,0x1E8u,next,0x40010001u,next,saved,
        nt::SERVICE_QUERY_THREAD_ID,nt::SERVICE_QUERY_THREAD_ID));
    assert(!nt::user_service_trap_allowed(
        k,true,0x160u,0x1E8u,next,u,next,0x00010001u,
        nt::SERVICE_QUERY_THREAD_ID,nt::SERVICE_QUERY_THREAD_ID));
    assert(!nt::user_service_trap_allowed(
        k,true,0x160u,0x1E8u,next,u,next,saved,
        nt::SERVICE_QUERY_THREAD_ID,nt::SERVICE_QUERY_PROCESS_ID));
    assert(!nt::user_service_trap_allowed(
        k,true,0x160u,0x1E8u,next,u,next,saved,
        0xDEADu,0xDEADu));
    uint32_t value=0u;
    assert(k.dispatch(next,u,0x8C000000u,0x1E8u,
                      nt::SERVICE_QUERY_PROCESS_ID,0u,&value)==nt::SUCCESS);
    assert(value==pid && k.thread.syscall_count==1u);
    assert(k.thread.last_trap.sgr==0x8C000000u &&
           k.thread.last_trap.spc==next &&
           k.thread.last_trap.ssr==u &&
           k.thread.last_trap.exception_event==0x160u);
    assert(k.dispatch(next,u,0x8C000000u,0x1E8u,
                      nt::SERVICE_QUERY_THREAD_ID,0u,&value)==nt::SUCCESS);
    assert(value==tid && k.thread.syscall_count==2u);
    assert(k.dispatch(next,u,0x8C000000u,0x1E8u,
                      nt::SERVICE_YIELD_EXECUTION,0u,&value)==nt::SUCCESS);
    assert(value==nt::SUCCESS && k.thread.syscall_count==3u &&
           k.thread.voluntary_yields==1u);
    assert(k.dispatch(next,0x40000000u,0x8C000000u,0x1E8u,
                      nt::SERVICE_QUERY_THREAD_ID,0u,&value)==nt::INVALID_PARAMETER);
    assert(k.dispatch(next,u,0x8C000000u,0x1ECu,
                      nt::SERVICE_QUERY_THREAD_ID,0u,&value)==nt::INVALID_PARAMETER);
    assert(k.dispatch(next,u,0x8C000000u,0x1E8u,
                      0xDEADu,0u,&value)==nt::INVALID_PARAMETER);
    assert(k.thread.rejected_traps==3u && k.thread.syscall_count==3u);
    k.end();
    assert(!k.process.active &&
           k.thread.state==nt::KTHREAD_STATE::terminated &&
           !k.running());
    assert(k.dispatch(next,u,0x8C000000u,0x1E8u,
                      nt::SERVICE_QUERY_PROCESS_ID,0u,&value)==nt::INVALID_PARAMETER);
    ps.finish();
    assert(!ps.active);
    assert(ps.begin());
    assert(ps.ke.begin(0x00400000u,0x10000u)==nt::SUCCESS);
    assert(ps.ke.process.unique_process_id!=pid &&
           ps.ke.thread.unique_thread_id!=tid);
    ps.finish();
    std::puts("PASS Ke/Ps KPROCESS/KTHREAD: foreground lifetime, trap frame, banked syscall checks, 3 services, yield, stale process IDs");
}
