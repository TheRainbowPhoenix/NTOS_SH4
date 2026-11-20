#include "../src/nt/virtual_memory.hpp"
#include "../src/fault_pager.hpp"
#include <cassert>
#include <cstdio>
struct Sim {
    nt::VirtualMemory<64,8> vm{};
    ntos_fault::Pager pager{};
    alignas(4096) uint8_t ram[64u*4096u]{};
    unsigned shots=0u;
    unsigned fail_on=0u;
    static bool resolve(void *p,uint32_t va,uint32_t *pa,ntos_mm::Access *a) {
        return static_cast<Sim*>(p)->vm.resolve(va,pa,a);
    }
    static bool install(void *,uint32_t,uint32_t,unsigned,unsigned) {
        return true;
    }
    static bool hw(void *p,unsigned,uint32_t,uint32_t) {
        auto &x=*static_cast<Sim*>(p);
        return ++x.shots!=x.fail_on; // simulates BOTH TLBs invalidated
    }
    static bool shoot(void *p,uint32_t va) {
        auto &x=*static_cast<Sim*>(p);
        return x.pager.invalidate(va,&hw,p);
    }
};
int main() {
    Sim s{};s.vm.configure(s.ram,0x0C000000u);
    s.pager.set_extra(&s,&Sim::resolve);
    uint32_t va=0u;
    assert(s.vm.reserve(0u,2u,&va)==nt::SUCCESS);
    assert(s.vm.commit(va,2u,nt::VmAccess::read_write)==nt::SUCCESS);
    assert(s.pager.fault(va,&Sim::install,&s));
    assert(s.pager.fault(va+4096u,&Sim::install,&s));
    const auto first=s.vm.pages[s.vm.find(va)].frame;
    s.fail_on=2u;
    assert(s.vm.decommit(va,2u,&Sim::shoot,&s)==nt::DEVICE_BUSY);
    assert(s.vm.committed_pages==2u);
    assert(s.vm.pages[s.vm.find(va)].frame==first);
    assert(s.pager.lookup(va)<0); // earlier invalidation is safe
    assert(s.pager.lookup(va+4096u)>=0);
    s.fail_on=0u;
    assert(s.vm.decommit(va,2u,&Sim::shoot,&s)==nt::SUCCESS);
    assert(s.pager.lookup(va+4096u)<0 && s.vm.committed_pages==0u);
    assert(s.vm.release(va)==nt::SUCCESS);
    uint32_t again=0u;
    assert(s.vm.reserve(va,1u,&again)==nt::SUCCESS && again==va);
    assert(s.vm.commit(again,1u,nt::VmAccess::read_write)==nt::SUCCESS);
    assert(s.vm.pages[s.vm.find(again)].frame==first && s.ram[0]==0u);
    std::puts("PASS simulated UTLB/ITLB shootdown: refuse failed ack, no frame reuse before ack, zero on recommit");
}
