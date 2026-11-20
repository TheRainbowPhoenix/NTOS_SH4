// Host-only Ps/Mm preflight: two KPROCESS PE section graphs can reuse
// the same virtual code/data addresses but NEVER private physical pages.
// No HH3 MMU switching is claimed by this test.
#include "../src/nt/ps_address_spaces.hpp"
#include <cassert>
#include <cstdio>
struct Flush {
    bool ack=true;
    uint32_t attempts=0u;
    static bool go(void *ctx,uint32_t source,uint32_t target) {
        auto &self=*static_cast<Flush *>(ctx);
        assert(source!=target);
        ++self.attempts;
        return self.ack; // stand-in for IRQ-masked UTLB+ITLB readback
    }
};
int main() {
    alignas(4096) static uint8_t vm_a[64u*4096u]{};
    alignas(4096) static uint8_t vm_b[64u*4096u]{};
    nt::VirtualMemory<64,8> a{},b{};
    a.configure(vm_a,0x0C000000u);
    b.configure(vm_b,0x0C100000u);
    nt::ProcessSpaces<2> ps{};
    uint32_t parent=0u,child=0u;
    assert(ps.create(0u,&a,0x00400000u,8192u,&parent)==nt::SUCCESS);
    assert(ps.create(parent,&b,0x00400000u,8192u,&child)==nt::SUCCESS);
    assert(parent!=child &&
           ps.find(child)->process.inherited_from_process_id==parent);
    using A=ntos_mm::Access;
    // Same VA / different physical text & data for independently linked
    // EXEs. RO shared COREDLL is intentionally identical in both roots.
    assert(ps.add_image(parent,0x00400000u,0x0A000000u,1u,A::read_only)==nt::SUCCESS);
    assert(ps.add_image(parent,0x00401000u,0x0A001000u,1u,A::read_write)==nt::SUCCESS);
    assert(ps.add_image(child,0x00400000u,0x0A100000u,1u,A::read_only)==nt::SUCCESS);
    assert(ps.add_image(child,0x00401000u,0x0A101000u,1u,A::read_write)==nt::SUCCESS);
    assert(ps.add_image(parent,0x01000000u,0x0B000000u,1u,A::read_only,true)==nt::SUCCESS);
    assert(ps.add_image(child,0x01000000u,0x0B000000u,1u,A::read_only,true)==nt::SUCCESS);
    assert(ps.add_image(child,0x01001000u,0x0B000000u,1u,A::read_only,true)==nt::CONFLICTING_ADDRESSES);
    assert(ps.add_image(child,0x00402000u,0x0A001000u,1u,A::read_write)==nt::CONFLICTING_ADDRESSES);
    assert(ps.add_image(child,0x01002000u,0x0B000000u,1u,A::read_write,true)==nt::INVALID_PARAMETER);
    assert(ps.add_image(child,0x00402000u,0x0C000000u,1u,A::read_only)==nt::CONFLICTING_ADDRESSES);
    assert(ps.add_image(child,0x00402000u,0x0C100000u,1u,A::read_only)==nt::CONFLICTING_ADDRESSES);
    assert(ps.add_image(child,0x00400000u,0x0A200000u,1u,A::read_only)==nt::CONFLICTING_ADDRESSES);
    assert(ps.add_image(child,0x06000000u,0x0A200000u,1u,A::read_only)==nt::INVALID_PARAMETER);
    assert(ps.add_image(child,0x08100000u,0x0A200000u,1u,A::read_only)==nt::INVALID_PARAMETER);
    assert(ps.add_image(child,0x80000000u,0x0A200000u,1u,A::read_only)==nt::INVALID_PARAMETER);
    assert(ps.add_image(child,0x00402000u,0x0A200000u,0u,A::read_only)==nt::INVALID_PARAMETER);
    uint32_t physical=0u;A rights=A::no_access;
    assert(!ps.resolve_current(0x00400000u,&physical,&rights));
    assert(!ps.find(parent)->root.resolve(0x06000000u,&physical,&rights));
    assert(ps.switch_to(parent,nullptr,nullptr)==nt::SUCCESS);
    assert(ps.add_image(parent,0x00402000u,0x0A200000u,1u,A::read_only)==nt::DEVICE_BUSY);
    Flush gate{};
    for(unsigned round=0u;round<1000u;++round) {
        const uint32_t current=(round&1u)?child:parent;
        const uint32_t other=(round&1u)?parent:child;
        assert(ps.current_pid==current);
        assert(ps.resolve_current(0x00400000u,&physical,&rights));
        assert(physical==((round&1u)?0x0A100000u:0x0A000000u));
        assert(rights==A::read_only);
        assert(ps.resolve_current(0x00401000u,&physical,&rights));
        assert(physical==((round&1u)?0x0A101000u:0x0A001000u));
        assert(rights==A::read_write);
        assert(ps.resolve_current(0x01000000u,&physical,&rights));
        assert(physical==0x0B000000u && rights==A::read_only);
        if(round==57u) {
            gate.ack=false;
            assert(ps.switch_to(other,&Flush::go,&gate)==nt::DEVICE_BUSY);
            assert(ps.current_pid==current &&
                   ps.resolve_current(0x00401000u,&physical,&rights));
            assert(physical==0x0A101000u);
            gate.ack=true;
        }
        assert(ps.switch_to(other,&Flush::go,&gate)==nt::SUCCESS);
    }
    assert(ps.current_pid==parent && gate.attempts==1001u);
    assert(ps.find(parent)->process.unique_process_id==parent &&
           ps.find(child)->process.unique_process_id==child);
    std::puts("PASS Ps host: 1000 dual-PE-root switches, same text/data VA with distinct PA, shared COREDLL RO, PPID, deny stale root / TLB failure");
}
