#include "../src/nt/address_space.hpp"
#include <cassert>
#include <cstdio>
int main() {
    using VM=nt::VirtualMemory<64,8>;
    alignas(4096) static uint8_t a[64u*4096u]{};
    alignas(4096) static uint8_t b[64u*4096u]{};
    VM va{},vb{};va.configure(a,0x0C000000u);
    vb.configure(b,0x0C100000u);
    nt::AddressSpaceRoot ra{},rb{};
    uint32_t current_a=257u,current_b=258u;
    assert(ra.bind(257u,&current_a,&va));
    assert(rb.bind(258u,&current_b,&vb));
    uint32_t x=0u,y=0u;
    assert(va.reserve(0u,2u,&x)==nt::SUCCESS);
    assert(vb.reserve(0u,2u,&y)==nt::SUCCESS && x==y);
    assert(va.commit(x,1u,nt::VmAccess::read_write)==nt::SUCCESS);
    assert(vb.commit(y,1u,nt::VmAccess::read_write)==nt::SUCCESS);
    a[0]=0xA5u;b[0]=0x5Au;
    uint32_t pa=0u,pb=0u;ntos_mm::Access aa{},bb{};
    assert(ra.resolve(x,&pa,&aa) && rb.resolve(y,&pb,&bb));
    assert(pa==0x0C000000u && pb==0x0C100000u && pa!=pb);
    assert(a[0]==0xA5u && b[0]==0x5Au);
    current_a=258u;
    assert(!ra.valid() && !ra.resolve(x,&pa,&aa));
    assert(rb.resolve(y,&pb,&bb));
    current_a=257u;ra.unbind();
    assert(!ra.resolve(x,&pa,&aa));
    va.clear();va.configure(a,0x0C200000u);
    assert(!ra.bind(259u,&current_a,&va));
    current_a=259u;assert(ra.bind(259u,&current_a,&va));
    assert(ra.epoch==2u && rb.epoch==1u);
    assert(!ra.resolve(x,&pa,&aa));
    rb.unbind();
    std::puts("PASS address-space roots: same VA, distinct PA, PID guard, teardown");
}
