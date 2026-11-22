/* Host integration preflight: truly destroy/recreate child Ps roots only
 * after Ke verified P4 UTLB+ITLB ack. This is NOT physical CP400 run.
 */
#include "../src/nt/ps_address_spaces.hpp"
#include <cassert>
#include <cstdio>
struct Revoke {
    bool ack=true;
    uint32_t attempts=0u,refusals=0u;
    static bool go(void *opaque,uint32_t current,uint32_t target) {
        auto &g=*static_cast<Revoke *>(opaque);
        assert(current && target && current!=target);
        ++g.attempts;
        if(!g.ack){++g.refusals;return false;}
        return true;
    }
};
int main() {
    alignas(4096) static uint8_t p1_parent[64u*4096u]{};
    alignas(4096) static uint8_t p1_child[64u*4096u]{};
    nt::VirtualMemory<64,8> parent_vm{},child_vm{};
    parent_vm.configure(p1_parent,0x0C000000u);
    uint32_t parent_region=0u;
    assert(parent_vm.reserve(0u,1u,&parent_region)==nt::SUCCESS);
    assert(parent_vm.commit(parent_region,1u,nt::VmAccess::read_write)==nt::SUCCESS);
    nt::ProcessSpaces<2u> ps{};
    uint32_t parent=0u;
    assert(ps.create(0u,&parent_vm,0x00400000u,8192u,&parent)==nt::SUCCESS);
    using A=ntos_mm::Access;
    assert(ps.add_image(parent,0x00400000u,0x0A000000u,1u,A::read_only)==nt::SUCCESS);
    assert(ps.add_image(parent,0x00401000u,0x0A001000u,1u,A::read_write)==nt::SUCCESS);
    assert(ps.add_image(parent,0x01000000u,0x0B000000u,1u,A::read_only,true)==nt::SUCCESS);
    assert(ps.switch_to(parent,nullptr,nullptr)==nt::SUCCESS);
    assert(ps.add_image(parent,0x00402000u,0x0A002000u,1u,A::read_write)==nt::DEVICE_BUSY);
    Revoke revoke{};
    uint32_t last_child=parent;
    for(unsigned i=0u;i<500u;++i) {
        child_vm.configure(p1_child,0x0C100000u);
        uint32_t region=0u,child=0u;
        assert(child_vm.reserve(0u,1u,&region)==nt::SUCCESS);
        assert(child_vm.commit(region,1u,nt::VmAccess::read_write)==nt::SUCCESS);
        assert(ps.create(parent,&child_vm,0x00400000u,8192u,&child)==nt::SUCCESS);
        assert(child>last_child && ps.find(child)!=nullptr);
        assert(ps.find(child)->process.inherited_from_process_id==parent);
        assert(ps.add_image(child,0x00400000u,0x0A100000u,1u,A::read_only)==nt::SUCCESS);
        assert(ps.add_image(child,0x00401000u,0x0A101000u,1u,A::read_write)==nt::SUCCESS);
        assert(ps.add_image(child,0x01000000u,0x0B000000u,1u,A::read_only,true)==nt::SUCCESS);
        assert(ps.add_image(child,0x00402000u,0x0A001000u,1u,A::read_write)==nt::CONFLICTING_ADDRESSES);
        assert(ps.add_image(parent,0x00402000u,0x0A002000u,1u,A::read_write)==nt::DEVICE_BUSY);
        assert(ps.switch_to(child,&Revoke::go,&revoke)==nt::SUCCESS);
        uint32_t pa=0u;A rights=A::no_access;
        assert(ps.resolve_current(0x00400000u,&pa,&rights) &&
               pa==0x0A100000u && rights==A::read_only);
        assert(ps.resolve_current(parent_region,&pa,&rights) &&
               pa==0x0C100000u && rights==A::read_write);
        p1_child[0]=static_cast<uint8_t>(i^0xABu);
        assert(ps.switch_to(parent,&Revoke::go,&revoke)==nt::SUCCESS);
        assert(ps.resolve_current(0x00400000u,&pa,&rights) &&
               pa==0x0A000000u);
        assert(ps.resolve_current(parent_region,&pa,&rights) &&
               pa==0x0C000000u);
        assert(!ps.find(child)->root.valid());
        if(i%17u==0u) {
            revoke.ack=false;
            assert(ps.destroy(child,&Revoke::go,&revoke)==nt::DEVICE_BUSY);
            assert(ps.find(child) && child_vm.committed_pages==1u &&
                   ps.current_pid==parent);
            revoke.ack=true;
        }
        assert(ps.destroy(child,nullptr,nullptr)==nt::DEVICE_BUSY);
        assert(ps.destroy(child,&Revoke::go,&revoke)==nt::SUCCESS);
        assert(!ps.find(child) && !child_vm.backing &&
               child_vm.committed_pages==0u && child_vm.reserved_pages==0u);
        assert(ps.current_pid==parent && ps.find(parent)->root.valid());
        // Never recycle a PID or VM frame before the revoke callback.
        last_child=child;
    }
    assert(ps.find(parent) && ps.current_pid==parent && revoke.refusals==30u);
    assert(ps.destroy(parent,nullptr,nullptr)==nt::DEVICE_BUSY);
    std::puts("PASS Ps lifecycle host: 500 child PE roots + owned VM, deny early teardown, mandatory P4 ack, parent live, monotonic PID and zero-on-reuse contract");
}
