#include "../src/nt/ps_address_spaces.hpp"
#include "../src/nt/scheduler.hpp"
#include <cassert>
#include <cstdio>
struct Worlds {
    nt::ProcessSpaces<2> spaces{};
    bool flush_ok=true;
    unsigned switches=0u;
    static bool flush(void *opaque,uint32_t from,uint32_t to) {
        auto &w=*static_cast<Worlds *>(opaque);
        if(!w.flush_ok)return false;
        const auto s=w.spaces.switch_to(to,[](void *,uint32_t,uint32_t) {
            return true; // simulated all-UTLB+ITLB acknowledged
        },nullptr);
        if(s==nt::SUCCESS && from!=to)++w.switches;
        return s==nt::SUCCESS;
    }
};
int main() {
    alignas(4096) static uint8_t ram_a[64u*4096u]{};
    alignas(4096) static uint8_t ram_b[64u*4096u]{};
    nt::VirtualMemory<64,8> a{},b{};
    a.configure(ram_a,0x0C000000u);
    b.configure(ram_b,0x0C100000u);
    uint32_t va=0u,vb=0u;
    assert(a.reserve(0u,1u,&va)==nt::SUCCESS);
    assert(b.reserve(0u,1u,&vb)==nt::SUCCESS && va==vb);
    assert(a.commit(va,1u,nt::VmAccess::read_write)==nt::SUCCESS);
    assert(b.commit(vb,1u,nt::VmAccess::read_write)==nt::SUCCESS);
    Worlds worlds{};
    uint32_t parent=0u,child=0u;
    assert(worlds.spaces.create(0u,&a,0x00400000u,4096u,&parent)==nt::SUCCESS);
    assert(worlds.spaces.create(parent,&b,0x00400000u,4096u,&child)==nt::SUCCESS);
    assert(parent!=child && worlds.spaces.find(child)->process.inherited_from_process_id==parent);
    assert(worlds.spaces.switch_to(parent,[](void *,uint32_t,uint32_t){return true;},nullptr)==nt::SUCCESS);
    nt::SH4Context contexts[2]{};contexts[0].complete=contexts[1].complete=true;
    contexts[0].gpr[15]=0x80000100u;contexts[1].gpr[15]=0x80000200u;
    nt::CooperativeScheduler<2> q{};
    assert(q.register_thread(0u,parent,65537u,contexts[0])==nt::SUCCESS);
    assert(q.register_thread(1u,child,65538u,contexts[1])==nt::SUCCESS);
    assert(q.begin(0u)==nt::SUCCESS);
    unsigned next=2u;nt::SH4Context resumed{};
    worlds.flush_ok=false;
    assert(q.yield(contexts[0],&next,&resumed,&Worlds::flush,&worlds)==nt::DEVICE_BUSY);
    assert(q.active==0u && worlds.spaces.current_pid==parent);
    worlds.flush_ok=true;
    uint32_t counters[2]{};
    for(unsigned i=0u;i<1000u;++i) {
        const unsigned cur=q.active;
        const uint32_t pid=cur?child:parent;
        assert(worlds.spaces.current_pid==pid);
        uint32_t pa=0u;ntos_mm::Access rights{};
        assert(worlds.spaces.resolve_current(va,&pa,&rights));
        assert(pa==(cur?0x0C100000u:0x0C000000u));
        ++counters[cur];
        (cur?ram_b:ram_a)[0]=static_cast<uint8_t>(counters[cur]);
        assert(q.yield(contexts[cur],&next,&resumed,&Worlds::flush,&worlds)==nt::SUCCESS);
        assert(next==(cur^1u));
    }
    assert(counters[0]==500u && counters[1]==500u);
    assert(worlds.switches==1000u && worlds.spaces.current_pid==parent);
    assert(ram_a[0]==(500u&255u) && ram_b[0]==(500u&255u));
    std::puts("PASS Ps/Ke host dual EXE: 1000 alternating steps, same VA distinct physical, parent PID, gate rollback");
}
