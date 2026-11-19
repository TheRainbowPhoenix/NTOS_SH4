// ex: tagged, bounded bookkeeping on top of the host's existing allocator.
// Never invoked from a hardware MMU/IRQ exception. Does not own physical RAM.
// Overflow, unknown pointer, wrong tag and double-free all fail closed.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ntstatus.hpp"
namespace nt {
constexpr uint32_t tag(char a,char b,char c,char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b<<8) |
           ((uint32_t)(uint8_t)c<<16) | ((uint32_t)(uint8_t)d<<24);
}
using Allocate=void *(*)(void *,size_t);
using Deallocate=void (*)(void *,void *);
template<unsigned TRACKED=128u>
struct Pool {
    struct Allocation {void *ptr=nullptr;uint32_t bytes=0u,tag=0u;};
    Allocation allocations[TRACKED]{};
    void *context=nullptr;
    Allocate allocator=nullptr;
    Deallocate deallocator=nullptr;
    uint32_t current=0u,peak=0u,failures=0u,invalid_frees=0u;
    void configure(void *ctx,Allocate alloc,Deallocate free_fn) {
        context=ctx;allocator=alloc;deallocator=free_fn;
    }
    void *alloc(uint32_t bytes,uint32_t name) {
        if(!allocator || !deallocator || !bytes ||
           bytes>0x7fffffffu || bytes>0xffffffffu-current) {
            ++failures;return nullptr;
        }
        unsigned i=0u;
        while(i<TRACKED && allocations[i].ptr)++i;
        if(i==TRACKED){++failures;return nullptr;}
        void *p=allocator(context,bytes);
        if(!p){++failures;return nullptr;}
        allocations[i]={p,bytes,name};
        current+=bytes;
        if(current>peak)peak=current;
        return p;
    }
    Status free(void *ptr,uint32_t name) {
        if(!ptr)return SUCCESS;
        for(unsigned i=0u;i<TRACKED;++i) {
            Allocation &a=allocations[i];
            if(a.ptr!=ptr)continue;
            if(a.tag!=name){++invalid_frees;return INVALID_PARAMETER;}
            if(!deallocator)return NOT_SUPPORTED;
            current-=a.bytes;
            a={};
            deallocator(context,ptr);
            return SUCCESS;
        }
        ++invalid_frees;
        return INVALID_PARAMETER;
    }
    void release_all() {
        for(unsigned i=0u;i<TRACKED;++i) {
            Allocation &a=allocations[i];
            if(a.ptr && deallocator)deallocator(context,a.ptr);
            a={};
        }
        current=0u;
    }
};
} // namespace nt
