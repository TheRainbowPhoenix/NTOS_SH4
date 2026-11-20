#include "../src/nt/native_vm.hpp"
#include "../src/nt/virtual_memory.hpp"
#include <cassert>
#include <cstdio>
struct Rig {
    alignas(4096) uint8_t backing[64u*4096u]{};
    nt::VirtualMemory<64,8> vm{};
    bool deny=false;
    Rig() {vm.configure(backing,0x0C000000u);}
    static nt::Status reserve(void *o,uint32_t base,uint32_t pages,uint32_t *out) {
        return static_cast<Rig *>(o)->vm.reserve(base,pages,out);
    }
    static nt::Status commit(void *o,uint32_t base,uint32_t pages,uint32_t rights) {
        return static_cast<Rig *>(o)->vm.commit(base,pages,static_cast<nt::VmAccess>(rights));
    }
    static nt::Status release(void *o,uint32_t base) {
        return static_cast<Rig *>(o)->vm.release(base);
    }
    static bool shoot(void *o,uint32_t) {
        return !static_cast<Rig *>(o)->deny;
    }
    static nt::Status decommit(void *o,uint32_t base,uint32_t pages) {
        Rig &x=*static_cast<Rig *>(o);
        return x.vm.decommit(base,pages,&shoot,&x);
    }
};
int main() {
    Rig r{};
    nt::NativeVmOps ops{&r,&Rig::reserve,&Rig::commit,nullptr,
                        &Rig::release,&Rig::decommit};
    uint32_t va=0u,bytes=8192u;
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &va,0u,&bytes,nt::NT_MEM_RESERVE|nt::NT_MEM_COMMIT,
        nt::NT_PAGE_READWRITE)==nt::SUCCESS);
    assert(r.vm.committed_pages==2u);
    uint32_t sz=4096u;
    r.deny=true;
    assert(nt::nt_free_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &va,&sz,nt::NT_MEM_DECOMMIT)==nt::DEVICE_BUSY);
    assert(sz==4096u && r.vm.committed_pages==2u);
    r.deny=false;
    assert(nt::nt_free_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &va,&sz,nt::NT_MEM_DECOMMIT)==nt::SUCCESS);
    assert(r.vm.committed_pages==1u && r.vm.query(va)==nt::VmAccess::reserved);
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &va,0u,&sz,nt::NT_MEM_COMMIT,
        nt::NT_PAGE_READWRITE)==nt::SUCCESS);
    assert(r.vm.committed_pages==2u && r.backing[0]==0u);
    sz=8192u;
    assert(nt::nt_free_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &va,&sz,nt::NT_MEM_DECOMMIT)==nt::SUCCESS);
    sz=0u;
    assert(nt::nt_free_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &va,&sz,nt::NT_MEM_RELEASE)==nt::SUCCESS && va==0u);
    std::puts("PASS NtFreeVirtualMemory MEM_DECOMMIT: fail-closed, re-commit, release");
}
