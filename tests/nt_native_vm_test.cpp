#include "../src/nt/native_vm.hpp"
#include "../src/nt/guest_buffer.hpp"
#include "../src/nt/virtual_memory.hpp"
#include <cassert>
#include <cstdio>
#include <cstdint>

struct Rig {
    alignas(4096) uint8_t bytes[64u*4096u]{};
    nt::VirtualMemory<64u,8u> vm{};
    bool fail_commit=false;
    unsigned remap_calls=0u;
    Rig() {vm.configure(bytes,0x0C100000u);}
    static nt::Status reserve(void *p,uint32_t preferred,uint32_t pages,
                              uint32_t *out) {
        return static_cast<Rig *>(p)->vm.reserve(preferred,pages,out);
    }
    static nt::Status commit(void *p,uint32_t base,uint32_t pages,uint32_t rights) {
        Rig &r=*static_cast<Rig *>(p);
        if(r.fail_commit)return nt::NO_MEMORY;
        return r.vm.commit(base,pages,static_cast<nt::VmAccess>(rights));
    }
    static bool remap(void *p,uint32_t,ntos_mm::Access) {
        ++static_cast<Rig *>(p)->remap_calls;
        return true;
    }
    static nt::Status protect(void *p,uint32_t base,uint32_t pages,
                              uint32_t rights,uint32_t *previous) {
        Rig &r=*static_cast<Rig *>(p);
        return r.vm.protect(base,pages,static_cast<nt::VmAccess>(rights),
                            &Rig::remap,&r,previous);
    }
    static nt::Status release(void *p,uint32_t base) {
        return static_cast<Rig *>(p)->vm.release(base);
    }
    nt::NativeVmOps callbacks() {
        return {this,&Rig::reserve,&Rig::commit,
                     &Rig::protect,&Rig::release};
    }
};
int main() {
    // Malformed P0 pointers must be rejected before any kernel deref.
    auto allowed=[](void *,uint32_t va)->bool {
        return va>=0x00410000u && va<0x00412000u;
    };
    assert(nt::guest_writable_word(0x00410000u,allowed,nullptr));
    assert(nt::guest_writable_word(0x00411FFCu,allowed,nullptr));
    assert(!nt::guest_writable_word(0u,allowed,nullptr));
    assert(!nt::guest_writable_word(0x00410001u,allowed,nullptr));
    assert(!nt::guest_writable_word(0x00412000u,allowed,nullptr));
    assert(!nt::guest_writable_word(0x80000000u,allowed,nullptr));
    assert(!nt::guest_writable_word(0xA0002000u,allowed,nullptr));
    assert(!nt::guest_writable_word(0xF7000000u,allowed,nullptr));
    assert(!nt::guest_writable_word(0x00410000u,nullptr,nullptr));
    Rig rig{};
    const auto ops=rig.callbacks();
    uint32_t base=0u,bytes=8193u;
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &base,0u,&bytes,nt::NT_MEM_RESERVE|nt::NT_MEM_COMMIT,
        nt::NT_PAGE_READWRITE)==nt::SUCCESS);
    assert(base==nt::VM_BASE && bytes==3u*4096u);
    assert(rig.vm.reserved_pages==3u && rig.vm.committed_pages==3u);
    assert(rig.vm.query(base)==nt::VmAccess::read_write);
    uint32_t pb=base,psz=2u*4096u,old=0u;
    assert(nt::nt_protect_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &pb,&psz,nt::NT_PAGE_READONLY,&old)==nt::SUCCESS);
    assert(old==nt::NT_PAGE_READWRITE && rig.remap_calls==2u &&
           rig.vm.query(base)==nt::VmAccess::read_only);
    old=0u;
    assert(nt::nt_protect_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &pb,&psz,nt::NT_PAGE_READWRITE,&old)==nt::SUCCESS);
    assert(old==nt::NT_PAGE_READONLY &&
           rig.vm.query(base)==nt::VmAccess::read_write);
    uint32_t release_base=base,release_bytes=0u;
    // Absolute rule: no live committed page release without UTLB shootdown.
    assert(nt::nt_free_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &release_base,&release_bytes,nt::NT_MEM_RELEASE)==nt::DEVICE_BUSY);
    assert(release_base==base && rig.vm.committed_pages==3u);
    assert(nt::nt_free_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &release_base,&release_bytes,nt::NT_MEM_DECOMMIT)==nt::INVALID_PARAMETER);

    uint32_t reserved=0u,res_size=4096u;
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &reserved,0u,&res_size,nt::NT_MEM_RESERVE,
        nt::NT_PAGE_READONLY)==nt::SUCCESS);
    assert(reserved!=base && rig.vm.reserved_pages==4u &&
           rig.vm.committed_pages==3u);
    uint32_t commit_size=4096u,commit_addr=reserved;
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &commit_addr,0u,&commit_size,nt::NT_MEM_COMMIT,
        nt::NT_PAGE_READWRITE)==nt::SUCCESS);
    assert(rig.vm.committed_pages==4u);
    // Intentional failing commit into a NEW reservation must rollback it.
    rig.fail_commit=true;
    uint32_t rejected=0u,rejected_size=4096u;
    const uint32_t before=rig.vm.reserved_pages;
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &rejected,0u,&rejected_size,
        nt::NT_MEM_RESERVE|nt::NT_MEM_COMMIT,
        nt::NT_PAGE_READWRITE)==nt::NO_MEMORY);
    assert(rejected==0u && rig.vm.reserved_pages==before &&
           rejected_size==4096u);
    rig.fail_commit=false;
    rejected=0u;
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &rejected,0u,&rejected_size,nt::NT_MEM_RESERVE,
        nt::NT_PAGE_READONLY)==nt::SUCCESS);
    assert(nt::nt_free_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &rejected,&release_bytes,nt::NT_MEM_RELEASE)==nt::SUCCESS);
    assert(rejected==0u);

    // Status and output values are unchanged on any failed NT operation.
    uint32_t broken=base,broken_size=0u;
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &broken,0u,&broken_size,nt::NT_MEM_COMMIT,
        nt::NT_PAGE_READWRITE)==nt::INVALID_PARAMETER);
    assert(broken==base && broken_size==0u);
    broken=0u;broken_size=4096u;
    assert(nt::nt_allocate_virtual_memory(ops,0xABCDEF01u,
        &broken,0u,&broken_size,nt::NT_MEM_RESERVE,
        nt::NT_PAGE_READWRITE)==nt::INVALID_PARAMETER);
    assert(!broken);
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &broken,0u,&broken_size,nt::NT_MEM_DECOMMIT,
        nt::NT_PAGE_READWRITE)==nt::INVALID_PARAMETER);
    assert(nt::nt_allocate_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &broken,0u,&broken_size,nt::NT_MEM_RESERVE,
        0x40u)==nt::INVALID_PARAMETER); // SH4A has no NX/PAGE_EXECUTE
    assert(nt::nt_protect_virtual_memory(ops,nt::NT_CURRENT_PROCESS,
        &pb,&psz,0x40u,&old)==nt::INVALID_PARAMETER);
    // Cross-launch teardown only after VBR/MMU/UTLB restoration.
    rig.vm.clear();
    assert(rig.vm.committed_pages==0u && rig.vm.reserved_pages==0u);
    std::puts("PASS native NT VM: reserve|commit, RW/RO PR, old protect, rollback, no committed release, invalid flags and NTSTATUS");
}
