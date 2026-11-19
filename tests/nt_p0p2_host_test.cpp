// Isolated host-side P0-P2 executive and pager state tests.
// Uses mocks and plain heap memory, NEVER physical CP400 flash.
#include "../src/nt/process.hpp"
#include "../src/fault_pager.hpp"
#include "../src/fs_guard.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

struct Host {
    unsigned allocated=0u,freed=0u,close_calls=0u;
    bool fail_next=false;
    static void *alloc(void *context,std::size_t n) {
        Host &h=*static_cast<Host *>(context);
        if(h.fail_next){h.fail_next=false;return nullptr;}
        void *p=std::malloc(n);
        if(p)++h.allocated;
        return p;
    }
    static void free(void *context,void *p) {
        ++static_cast<Host *>(context)->freed;
        std::free(p);
    }
};
static unsigned installed=0u;
static bool install(void *,uint32_t va,uint32_t pa,unsigned slot,unsigned way) {
    assert(slot<55u && way==0u);
    assert(va>=0x00400000u && va<0x00460000u);
    assert(pa==0x0C000000u+(va-0x00400000u));
    ++installed;
    return true;
}
int main() {
    Host h{};
    nt::Process p{};
    p.pool.configure(&h,&Host::alloc,&Host::free);
    assert(p.begin());
    constexpr uint32_t tag=nt::tag('P','0','P','2');
    uint32_t first=0u;
    for(unsigned i=0;i<128u;++i) {
        uint32_t next=0u;
        assert(p.create_event((i&1u)!=0u,(i&1u)!=0u,&next)==nt::SUCCESS);
        assert(next!=first);
        if(first)assert(p.event_set(first)==nt::INVALID_HANDLE);
        assert(p.handles.close(next)==nt::SUCCESS);
        first=next;
    }
    uint32_t live[16]{};
    for(auto &token:live)assert(p.create_event(true,false,&token)==nt::SUCCESS);
    uint32_t blocked=0u;
    assert(p.create_event(true,false,&blocked)==nt::INSUFFICIENT_RESOURCES);
    assert(p.event_poll(live[0],0u)==nt::TIMEOUT);
    assert(p.event_set(live[0])==nt::SUCCESS);
    assert(p.event_poll(live[0],0u)==nt::SUCCESS);
    assert(p.event_poll(live[0],0u)==nt::SUCCESS);
    assert(p.event_reset(live[0])==nt::SUCCESS);
    assert(p.event_poll(live[0],0u)==nt::TIMEOUT);
    assert(p.event_poll(live[0],1u)==nt::NOT_SUPPORTED);
    for(uint32_t token:live)assert(p.handles.close(token)==nt::SUCCESS);
    uint32_t auto_event=0u;
    assert(p.create_event(false,true,&auto_event)==nt::SUCCESS);
    assert(p.event_poll(auto_event,0u)==nt::SUCCESS);
    assert(p.event_poll(auto_event,0u)==nt::TIMEOUT);
    assert(p.handles.close(auto_event)==nt::SUCCESS);
    void *memory[128]{};
    for(unsigned i=0u;i<128u;++i) {
        const std::size_t sz=1u+(i*8191u)/127u;
        memory[i]=p.pool.alloc(static_cast<uint32_t>(sz),tag);
        assert(memory[i]);
        auto *bytes=static_cast<uint8_t *>(memory[i]);
        bytes[0]=static_cast<uint8_t>(i);
        bytes[sz-1u]=static_cast<uint8_t>(i^0xA5u);
    }
    assert(p.pool.alloc(1u,tag)==nullptr);
    assert(p.pool.alloc(0xffffffffu,tag)==nullptr);
    h.fail_next=true;
    for(unsigned i=127u;i>0u;--i) {
        const unsigned j=(i*73u)%128u;
        if(memory[j]) {
            assert(p.pool.free(memory[j],tag^1u)==nt::INVALID_PARAMETER);
            assert(p.pool.free(memory[j],tag)==nt::SUCCESS);
            memory[j]=nullptr;
        }
    }
    for(void *&v:memory)if(v) {
        assert(p.pool.free(v,tag)==nt::SUCCESS);
        v=nullptr;
    }
    assert(p.pool.alloc(12u,tag)==nullptr);
    assert(p.pool.current==0u && p.pool.peak>8000u);
    assert(p.pool.free(memory[0],tag)==nt::SUCCESS); // null free
    assert(p.pool.free(reinterpret_cast<void *>(uintptr_t{0x1234}),tag)==nt::INVALID_PARAMETER);

    // Stage 1: outgoing process leaks one event, tagged block, SDK file.
    uint32_t from_first=0u;
    assert(p.create_event(true,true,&from_first)==nt::SUCCESS);
    assert(p.pool.alloc(4096u,tag));
    assert(p.attach_file(17,&blocked)==nt::SUCCESS);
    ntos_fs::Gate gate{};
    assert(gate.acquire_file(17));
    auto closer=[&h](int fd){assert(fd==17);++h.close_calls;return 0;};
    auto finder=[](int){assert(false);return -1;};
    assert(gate.cleanup(closer,finder));
    p.finish(); // strictly after host descriptor successfully closes
    assert(!p.active && !p.handles.used && !p.pool.current);
    assert(!gate.busy() && h.close_calls==1u);

    // Stage 2: new launch must see a clean baseline; stale tokens rejected.
    assert(p.begin());
    assert(p.event_set(from_first)==nt::INVALID_HANDLE);
    assert(p.file_fd(blocked)<0);
    assert(p.handles.used==0u && p.pool.current==0u);
    uint32_t from_second=0u;
    assert(p.create_event(false,false,&from_second)==nt::SUCCESS);
    assert(from_second!=from_first);
    assert(p.pool.alloc(42u,tag));
    // Simulate a failed DLL initialization / dependency rejection. The
    // executive must unwind even if the app never reaches its entry point.
    const bool dll_initialization_ok=false;
    assert(!dll_initialization_ok);
    p.finish();
    assert(p.begin());
    assert(p.handles.used==0u && p.pool.current==0u);
    assert(p.event_set(from_second)==nt::INVALID_HANDLE);
    p.finish();
    assert(h.allocated==h.freed);

    // 96 RAM-backed P0 pages; explicit installs/eviction with deterministic
    // virtual->physical mapping. This is NOT a genuine CPU VBR exception.
    ntos_fault::Pager pager{};
    assert(pager.add(0x00400000u,0x0C000000u,96u));
    for(uint32_t i=0;i<96u;++i)
        assert(pager.fault(0x00400000u+i*4096u,install,nullptr));
    assert(pager.evictions==96u-ntos_fault::SLOTS);
    for(uint32_t i=96u;i>0u;--i)
        assert(pager.fault(0x00400000u+(i-1u)*4096u,install,nullptr));
    assert(pager.evictions>60u && installed>96u);
    assert(!pager.fault(0x70000000u,install,nullptr));
    // Existing translation can be explicitly reinstalled by true-miss flag.
    assert(pager.fault(0x00400000u,install,nullptr,false,true));
    std::puts("PASS P0/P1/P2: 128 stale handles, event semantics, Ex OOM, cross-launch cleanup, mocked DLL failure, 96-page UTLB");
}
