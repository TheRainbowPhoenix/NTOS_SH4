// First bounded NT-native VM API layer for the single foreground process.
// Separate from the SH4A HAL and PE section policy. No SDK operations here.
// Passing NT process pseudo-handle -1 is supported; cross-process handles,
// SEC_IMAGE mappings, pagefile, MEM_DECOMMIT and executable/NX protections
// are explicitly unsupported. Preserve the no-live-physical-frame-reuse rule.
#pragma once
#include <stdint.h>
#include "ntstatus.hpp"

namespace nt {
constexpr uint32_t NT_CURRENT_PROCESS=0xFFFFFFFFu;
constexpr uint32_t NT_MEM_COMMIT=0x00001000u;
constexpr uint32_t NT_MEM_RESERVE=0x00002000u;
constexpr uint32_t NT_MEM_DECOMMIT=0x00004000u;
constexpr uint32_t NT_MEM_RELEASE=0x00008000u;
constexpr uint32_t NT_PAGE_READONLY=0x00000002u;
constexpr uint32_t NT_PAGE_READWRITE=0x00000004u;
constexpr uint32_t NT_VM_PAGE=4096u;
constexpr uint32_t NT_VM_CAPACITY_BYTES=64u*NT_VM_PAGE;

struct NativeVmOps {
    void *owner=nullptr;
    Status (*reserve)(void *,uint32_t,uint32_t,uint32_t *)=nullptr;
    Status (*commit)(void *,uint32_t,uint32_t,uint32_t)=nullptr;
    Status (*protect)(void *,uint32_t,uint32_t,uint32_t,uint32_t *)=nullptr;
    Status (*release)(void *,uint32_t)=nullptr;
    Status (*decommit)(void *,uint32_t,uint32_t)=nullptr;
};

inline uint32_t nt_page_to_rights(uint32_t protect) {
    return protect==NT_PAGE_READONLY?1u:
           protect==NT_PAGE_READWRITE?2u:0u;
}
inline uint32_t rights_to_nt_page(uint32_t rights) {
    return rights==1u?NT_PAGE_READONLY:
           rights==2u?NT_PAGE_READWRITE:0u;
}
inline Status nt_vm_pages(uint32_t bytes,uint32_t *out_pages) {
    if(!out_pages || !bytes || bytes>NT_VM_CAPACITY_BYTES)
        return INVALID_PARAMETER;
    *out_pages=(bytes/NT_VM_PAGE)+((bytes%NT_VM_PAGE)!=0u);
    return SUCCESS;
}
inline Status nt_allocate_virtual_memory(
    const NativeVmOps &ops,uint32_t process,uint32_t *base,
    uint32_t zero_bits,uint32_t *region_bytes,
    uint32_t type,uint32_t protection) {
    if(process!=NT_CURRENT_PROCESS || !base || !region_bytes ||
       zero_bits || !ops.reserve || !ops.commit || !ops.release)
        return INVALID_PARAMETER;
    const uint32_t legal=NT_MEM_RESERVE|NT_MEM_COMMIT;
    if(!(type&legal) || (type&~legal) || !nt_page_to_rights(protection))
        return INVALID_PARAMETER;
    const uint32_t old_base=*base,requested=*region_bytes;
    uint32_t pages=0u;
    Status s=nt_vm_pages(requested,&pages);
    if(s!=SUCCESS || (old_base&(NT_VM_PAGE-1u)))
        return INVALID_PARAMETER;
    if((type&NT_MEM_RESERVE)==0u && old_base==0u)
        return INVALID_PARAMETER;
    uint32_t placed=old_base;
    bool reserved_new=false;
    if(type&NT_MEM_RESERVE) {
        s=ops.reserve(ops.owner,old_base,pages,&placed);
        if(s!=SUCCESS)return s;
        reserved_new=true;
    }
    if(type&NT_MEM_COMMIT) {
        s=ops.commit(ops.owner,placed,pages,nt_page_to_rights(protection));
        if(s!=SUCCESS) {
            if(reserved_new && ops.release(ops.owner,placed)!=SUCCESS)
                return INTERNAL_ERROR;
            return s;
        }
    }
    *base=placed;
    *region_bytes=pages*NT_VM_PAGE;
    return SUCCESS;
}
inline Status nt_protect_virtual_memory(
    const NativeVmOps &ops,uint32_t process,uint32_t *base,
    uint32_t *region_bytes,uint32_t new_protection,
    uint32_t *old_protection) {
    if(process!=NT_CURRENT_PROCESS || !base || !region_bytes ||
       !old_protection || !ops.protect || !nt_page_to_rights(new_protection))
        return INVALID_PARAMETER;
    const uint32_t va=*base,bytes=*region_bytes;
    uint32_t count=0u;
    Status s=nt_vm_pages(bytes,&count);
    if(s!=SUCCESS || (va&(NT_VM_PAGE-1u)))return INVALID_PARAMETER;
    uint32_t prior=0u;
    s=ops.protect(ops.owner,va,count,nt_page_to_rights(new_protection),
                  &prior);
    if(s!=SUCCESS)return s;
    const uint32_t translated=rights_to_nt_page(prior);
    if(!translated)return INTERNAL_ERROR;
    *base=va;
    *region_bytes=count*NT_VM_PAGE;
    *old_protection=translated;
    return SUCCESS;
}
inline Status nt_free_virtual_memory(
    const NativeVmOps &ops,uint32_t process,uint32_t *base,
    uint32_t *region_bytes,uint32_t type) {
    if(process!=NT_CURRENT_PROCESS || !base || !region_bytes ||
       (*base&(NT_VM_PAGE-1u)) || *base==0u)
        return INVALID_PARAMETER;
    if(type==NT_MEM_DECOMMIT) {
        uint32_t pages=0u;
        if(nt_vm_pages(*region_bytes,&pages)!=SUCCESS)return INVALID_PARAMETER;
        if(!ops.decommit)return NOT_SUPPORTED;
        const Status status=ops.decommit(ops.owner,*base,pages);
        if(status==SUCCESS)*region_bytes=pages*NT_VM_PAGE;
        return status; // never modify in/out arguments after failure
    }
    if(type!=NT_MEM_RELEASE || *region_bytes!=0u || !ops.release)
        return INVALID_PARAMETER;
    const Status result=ops.release(ops.owner,*base);
    if(result==SUCCESS)*base=0u; // only after decommit or reserved-only release
    return result;
}
} // namespace nt
