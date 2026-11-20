#include "../src/nt/virtual_memory.hpp"
#include "../src/nt/scheduler.hpp"
#include <cassert>
#include <cstdio>
struct Slot {
    alignas(4096) uint8_t physical[64u*4096u]{};
    nt::VirtualMemory<64,8> vm{};
    bool ack=true;
    unsigned shot=0u;
    Slot(uint32_t base) {vm.configure(physical,base);}
    static bool shoot(void *opaque,uint32_t) {
        auto &s=*static_cast<Slot *>(opaque);
        ++s.shot;return s.ack;
    }
};
int main() {
    Slot a{0x0C000000u},b{0x0C100000u};
    Slot *world[2]{&a,&b};
    uint32_t va[2]{};
    for(unsigned j=0u;j<2u;++j) {
        assert(world[j]->vm.reserve(0u,2u,&va[j])==nt::SUCCESS);
        assert(world[j]->vm.commit(va[j],2u,nt::VmAccess::read_write)==nt::SUCCESS);
        world[j]->physical[4096]=0xA5u+j;
    }
    assert(va[0]==va[1]);
    for(unsigned round=0u;round<500u;++round) {
        Slot &s=*world[round&1u];
        s.physical[0]=static_cast<uint8_t>(round);
        s.physical[4095]=0xCCu;
        if(round==73u) {
            s.ack=false;
            assert(s.vm.decommit(va[round&1u],1u,&Slot::shoot,&s)==nt::DEVICE_BUSY);
            assert(s.vm.committed_pages==2u && s.physical[0]==static_cast<uint8_t>(round));
            s.ack=true;
        }
        assert(s.vm.decommit(va[round&1u],1u,&Slot::shoot,&s)==nt::SUCCESS);
        assert(s.vm.committed_pages==1u);
        assert(s.vm.commit(va[round&1u],1u,nt::VmAccess::read_write)==nt::SUCCESS);
        assert(s.physical[0]==0u && s.physical[4095]==0u);
        assert(s.physical[4096]==0xA5u+(round&1u));
    }
    assert(a.vm.committed_pages==2u && b.vm.committed_pages==2u);
    assert(a.shot>200u && b.shot>200u);
    std::puts("PASS simulated 500-cycle alternating two-thread decommit: shootdown failure preserves data; frames zeroed");
}
