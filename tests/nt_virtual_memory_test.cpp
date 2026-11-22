// Pure host validation of NT-like RAM-backed VM and 55-entry UTLB.
// No hardware registers, SDK descriptors or guest accesses involved.
#include "../src/nt/virtual_memory.hpp"
#include "../src/fault_pager.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

using VM=nt::VirtualMemory<64,8>;
struct Mock {
    VM vm{};
    ntos_fault::Pager pager{};
    unsigned mapped_ro=0u,mapped_rw=0u,mapped_priv=0u;
    unsigned fail_index=9999u,remaps=0u;
    alignas(4096) uint8_t data[64u*4096u]{};
    static bool collide(void *p,uint32_t va,uint32_t count) {
        auto &ctx=*static_cast<Mock *>(p);
        for(unsigned i=0u;i<ctx.pager.modules_used;++i) {
            const auto &m=ctx.pager.modules[i];
            if(ntos_rebase::overlap(va,count,m.va,m.pages))
                return true;
        }
        return false;
    }
    static bool resolve(void *p,uint32_t va,uint32_t *pa,
                        ntos_mm::Access *rights) {
        return static_cast<Mock *>(p)->vm.resolve(va,pa,rights);
    }
    static bool install(void *p,uint32_t va,uint32_t pa,
                        unsigned slot,unsigned ro) {
        auto &m=*static_cast<Mock *>(p);
        assert(slot<55u && (ro==0u || ro==1u || ro==2u));
        assert(pa>=0x0C000000u && pa<0x0C040000u);
        assert((va&0xFFFu)==0u);
        if(ro==2u)m.mapped_priv++;
        else if(ro==1u)m.mapped_ro++;
        else m.mapped_rw++;
        return true;
    }
    static bool update(void *p,uint32_t va,ntos_mm::Access rights) {
        auto &ctx=*static_cast<Mock *>(p);
        if(ctx.remaps++==ctx.fail_index)return false;
        return ctx.pager.reprotect(va,rights,&install,p);
    }
};
int main() {
    Mock m{};
    m.vm.configure(m.data,0x0C000000u);
    m.pager.set_extra(&m,&Mock::resolve);
    assert(m.pager.add(0x06000000u,0x0C060000u,4u)); // force first-fit skip
    uint32_t va=0u;
    assert(m.vm.reserve(0u,12u,&va,&Mock::collide,&m)==nt::SUCCESS);
    assert(va==0x06004000u);
    assert(m.vm.reserved_pages==12u && m.vm.committed_pages==0u);
    assert(m.vm.query(va)==nt::VmAccess::reserved);
    assert(!m.pager.fault(va,&Mock::install,&m));
    assert(m.vm.commit(va,8u,nt::VmAccess::read_write)==nt::SUCCESS);
    assert(m.vm.committed_pages==8u && m.vm.peak_pages==8u);
    assert(m.vm.query(va)==nt::VmAccess::read_write);
    assert(m.vm.commit(va,1u,nt::VmAccess::read_write)==nt::ALREADY_COMMITTED);
    assert(m.vm.commit(va+8u*4096u,1u,nt::VmAccess::reserved)==nt::INVALID_PARAMETER);
    assert(m.vm.commit(va+12u*4096u,1u,nt::VmAccess::read_write)==nt::INVALID_PARAMETER);
    for(uint32_t i=0u;i<8u;++i) {
        const uint32_t page=va+i*4096u;
        uint32_t pa=0u;ntos_mm::Access rights=ntos_mm::Access::no_access;
        assert(m.vm.resolve(page,&pa,&rights));
        assert(rights==ntos_mm::Access::read_write);
        assert(pa==0x0C000000u+i*4096u);
        assert(m.pager.fault(page,&Mock::install,&m));
        m.data[i*4096u]=static_cast<uint8_t>(i+1u);
    }
    assert(m.pager.fault(va,&Mock::install,&m,false,true));
    assert(m.mapped_rw==9u);
    uint32_t previous=0u;
    assert(m.vm.protect(va,4u,nt::VmAccess::read_only,&Mock::update,&m,
                        &previous)==nt::SUCCESS);
    assert(previous==static_cast<uint32_t>(nt::VmAccess::read_write));
    assert(m.vm.query(va)==nt::VmAccess::read_only);
    assert(m.vm.query(va+4u*4096u)==nt::VmAccess::read_write);
    assert(m.pager.access(va)==ntos_mm::Access::read_only);
    assert(m.pager.lookup(va)>=0 && m.pager.entries[m.pager.lookup(va)].access==
                               ntos_mm::Access::read_only);
    assert(m.vm.protect(va+8u*4096u,1u,nt::VmAccess::read_only,
                        &Mock::update,&m)==nt::NOT_COMMITTED);
    // Reprotect rollback must restore both metadata and installed rights.
    m.fail_index=m.remaps+1u;
    assert(m.vm.protect(va+4u*4096u,3u,nt::VmAccess::read_only,
                        &Mock::update,&m)==nt::DEVICE_BUSY);
    for(unsigned i=4u;i<7u;++i)
        assert(m.vm.query(va+i*4096u)==nt::VmAccess::read_write);
    m.fail_index=9999u;
    assert(m.vm.protect(va,4u,nt::VmAccess::read_write,
                        &Mock::update,&m)==nt::SUCCESS);
    assert(m.vm.query(va)==nt::VmAccess::read_write);

    // Never release or reuse committed physical frames with live P0 aliases.
    assert(m.vm.release(va)==nt::DEVICE_BUSY);
    assert(m.vm.commit(va+8u*4096u,4u,nt::VmAccess::read_only)==nt::SUCCESS);
    assert(m.vm.committed_pages==12u);
    assert(m.vm.reserve(va,1u,&previous,&Mock::collide,&m)==nt::CONFLICTING_ADDRESSES);
    assert(m.vm.commit(va+8u*4096u,4u,nt::VmAccess::read_only)==nt::ALREADY_COMMITTED);
    uint32_t other=0u;
    assert(m.vm.reserve(0u,53u,&other,&Mock::collide,&m)==nt::INSUFFICIENT_RESOURCES);
    assert(m.vm.reserve(0x07000000u,1u,&other,&Mock::collide,&m)==nt::INVALID_PARAMETER);
    assert(m.vm.reserve(0x06000000u,1u,&other,&Mock::collide,&m)==nt::CONFLICTING_ADDRESSES);
    assert(m.vm.reserve(0u,1u,&other,&Mock::collide,&m)==nt::SUCCESS);
    assert(m.vm.release(other)==nt::SUCCESS);
    assert(m.vm.query(other)==nt::VmAccess::reserved);
    assert(m.vm.committed_pages==12u);
    // Debug-only supervisor PR=01 is deliberately NOT exposed by normal
    // device VM syscalls. Its physical mapping must retain code 2.
    assert(m.vm.reserve(0u,1u,&other,&Mock::collide,&m)==nt::SUCCESS);
    assert(m.vm.commit(other,1u,nt::VmAccess::emulator_user_denied)==nt::SUCCESS);
    assert(m.pager.fault(other,&Mock::install,&m));
    assert(m.mapped_priv==1u && m.pager.access(other)==
                                  ntos_mm::Access::privileged_only);
    assert(m.vm.protect(other,1u,nt::VmAccess::read_write,
                        &Mock::update,&m)==nt::SUCCESS);
    assert(m.pager.access(other)==ntos_mm::Access::read_write);
    assert(m.vm.committed_pages==13u);
    m.vm.clear(); // only after caller restores host world
    assert(m.vm.committed_pages==0u && m.vm.reserved_pages==0u);
    assert(!m.vm.resolve(va,&previous,reinterpret_cast<ntos_mm::Access *>(&previous)));
    std::puts("PASS NT private VM: reserve, 12-page commit, P1 frame-backed faults, RO/RW repr, rollback, safe release/teardown");
}
