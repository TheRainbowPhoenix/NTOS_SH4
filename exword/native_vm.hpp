/* NT Native ABI adapter for an EX-word guest's private P1 VM arena.
 * Same NtAllocate/Protect/Free calling convention as the Hollyhock test,
 * but never installs P0 UTLB entries or changes ROM32A exception vectors.
 * Read/write callbacks must validate writable guest PE data (NOT P1 stack)
 * before dereferencing user-provided in/out pointers. */
#ifndef NTOS_EXWORD_NATIVE_VM_HPP
#define NTOS_EXWORD_NATIVE_VM_HPP
#include <stdint.h>
#include "p1_vm.hpp"
struct NtosExwordNativeVm {
    enum : uint32_t {
        CURRENT=0xffffffffu, RESERVE=0x2000u, COMMIT=0x1000u,
        DECOMMIT=0x4000u, RELEASE=0x8000u,
        PAGE_RO=2u, PAGE_RW=4u,
        OK=0u, INVALID=0xc000000du, NO_MEMORY=0xc0000017u,
        DEVICE_BUSY=0xc00000e8u
    };
    NtosP1Vm *vm=nullptr;
    bool (*read)(void *,uint32_t,uint32_t *)=nullptr;
    bool (*write)(void *,uint32_t,uint32_t)=nullptr;
    void *ctx=nullptr;
    void begin(NtosP1Vm *memory,
               bool (*r)(void *,uint32_t,uint32_t *),
               bool (*w)(void *,uint32_t,uint32_t),void *context) {
        vm=memory;read=r;write=w;ctx=context;
    }
    bool words(uint32_t base_ptr,uint32_t size_ptr,
               uint32_t *base,uint32_t *size) const {
        return vm && read && write && base && size &&
               read(ctx,base_ptr,base) && read(ctx,size_ptr,size);
    }
    static uint32_t pages_for(uint32_t bytes) {
        return bytes && bytes<=NtosP1Vm::MAX_PAGES*4096u ?
            (bytes+4095u)/4096u:0u;
    }
    uint32_t allocate(uint32_t process,uint32_t base_ptr,
                      uint32_t zero_bits,uint32_t size_ptr,
                      uint32_t type,uint32_t rights) {
        uint32_t base=0u,size=0u;
        if(process!=CURRENT || zero_bits || !words(base_ptr,size_ptr,&base,&size))
            return INVALID;
        uint32_t pages=pages_for(size);
        if(!pages || (rights!=PAGE_RO && rights!=PAGE_RW) ||
           (type!=RESERVE && type!=(RESERVE|COMMIT) && type!=COMMIT))
            return INVALID;
        if(type&RESERVE) {
            if(base)return INVALID;
            base=vm->reserve(0u,pages);
            if(!base)return NO_MEMORY;
        } else if(!base)return INVALID;
        if((type&COMMIT) && vm->change(base,pages,
                                      rights==PAGE_RW?2u:1u,true)!=0u)
            return NO_MEMORY;
        if(!write(ctx,base_ptr,base) ||
           !write(ctx,size_ptr,pages*4096u))return INVALID;
        return OK;
    }
    uint32_t protect(uint32_t process,uint32_t base_ptr,
                     uint32_t size_ptr,uint32_t rights,
                     uint32_t old_ptr) {
        uint32_t base=0u,size=0u,old=0u;
        if(process!=CURRENT || !words(base_ptr,size_ptr,&base,&size) ||
           !read || !read(ctx,old_ptr,&old) ||
           (rights!=PAGE_RO && rights!=PAGE_RW))return INVALID;
        uint32_t pages=pages_for(size);
        if(!pages || (base&4095u))return INVALID;
        const uint32_t previous=vm->query(base);
        if(previous!=1u && previous!=2u)return INVALID;
        if(vm->change(base,pages,rights==PAGE_RW?2u:1u,false)!=0u)
            return INVALID;
        return write(ctx,old_ptr,previous==2u?PAGE_RW:PAGE_RO)?OK:INVALID;
    }
    uint32_t free(uint32_t process,uint32_t base_ptr,
                  uint32_t size_ptr,uint32_t type) {
        uint32_t base=0u,size=0u;
        if(process!=CURRENT || !words(base_ptr,size_ptr,&base,&size))
            return INVALID;
        if(type==RELEASE) {
            if(size || !base || (base&4095u))return INVALID;
            if(vm->release(base)!=0u)
                return vm->query(base)!=0xffffffffu?DEVICE_BUSY:INVALID;
            return write(ctx,base_ptr,0u)?OK:INVALID;
        }
        if(type==DECOMMIT) {
            uint32_t pages=pages_for(size);
            if(!pages || (base&4095u) || vm->decommit(base,pages)!=0u)
                return INVALID;
            return OK;
        }
        return INVALID;
    }
};
#endif
