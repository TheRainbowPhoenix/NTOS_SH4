#include "../src/nt/ps_address_spaces.hpp"
#include <cassert>
#include <cstdio>
static bool accepted(void *p,uint32_t,uint32_t) {
    return *static_cast<bool *>(p);
}
int main() {
    alignas(4096) static uint8_t ram_a[64u*4096u]{};
    alignas(4096) static uint8_t ram_b[64u*4096u]{};
    nt::VirtualMemory<64,8> a{},b{};
    a.configure(ram_a,0x0C000000u);b.configure(ram_b,0x0C100000u);
    uint32_t va=0u,vb=0u;
    assert(a.reserve(0u,1u,&va)==nt::SUCCESS);
    assert(b.reserve(0u,1u,&vb)==nt::SUCCESS && va==vb);
    assert(a.commit(va,1u,nt::VmAccess::read_write)==nt::SUCCESS);
    assert(b.commit(vb,1u,nt::VmAccess::read_write)==nt::SUCCESS);
    ram_a[0]=0x34u;ram_b[0]=0xA7u;
    nt::ProcessSpaces<2> ps{};
    uint32_t parent=0u,child=0u;
    assert(ps.create(0u,&a,0x00400000u,4096u,&parent)==nt::SUCCESS);
    assert(ps.create(parent,&b,0x00400000u,4096u,&child)==nt::SUCCESS);
    assert(parent==257u && child==258u &&
           ps.find(child)->process.inherited_from_process_id==parent);
    assert(ps.create(parent,&a,0x00400000u,4096u,&va)==nt::CONFLICTING_ADDRESSES);
    bool allowed=true;
    assert(ps.switch_to(parent,&accepted,&allowed)==nt::SUCCESS);
    uint32_t phys=0u;ntos_mm::Access rights{};
    assert(ps.resolve_current(nt::VM_BASE,&phys,&rights));
    assert(phys==0x0C000000u && ps.find(child)->root.valid()==false);
    allowed=false;
    assert(ps.switch_to(child,&accepted,&allowed)==nt::DEVICE_BUSY);
    assert(ps.current_pid==parent && ps.resolve_current(nt::VM_BASE,&phys,&rights) &&
           phys==0x0C000000u);
    allowed=true;
    assert(ps.switch_to(child,&accepted,&allowed)==nt::SUCCESS);
    assert(ps.resolve_current(nt::VM_BASE,&phys,&rights) &&
           phys==0x0C100000u && !ps.find(parent)->root.valid());
    assert(ram_a[0]==0x34u && ram_b[0]==0xA7u);
    assert(ps.switch_to(parent,&accepted,&allowed)==nt::SUCCESS);
    assert(ps.resolve_current(nt::VM_BASE,&phys,&rights) &&
           phys==0x0C000000u);
    std::puts("PASS two KPROCESS spaces: parent PID, same VA/different PA, fail-closed switch, no cross-owner resolution");
}
