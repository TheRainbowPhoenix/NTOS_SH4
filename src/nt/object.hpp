// ob: fixed-capacity typed objects and non-repeating opaque handles.
// No allocations, exceptions, RTTI, SDK calls or seL4 dependencies.
// An old handle NEVER aliases a newly opened object within one NTOS boot:
// 30-bit cookies are issued monotonically and are not reused on overflow.
#pragma once
#include <stdint.h>
#include "ntstatus.hpp"
namespace nt {
enum class Type : uint8_t { none=0, file=1, finder=2, event=3 };
struct Object {
    Type type=Type::none;
    uint32_t references=0;
    void (*destroy)(Object *)=nullptr;
};
inline void init(Object &o,Type type,void (*destroy)(Object *)=nullptr) {
    o.type=type;o.references=1u;o.destroy=destroy;
}
inline bool reference(Object *o) {
    if(!o || o->type==Type::none || o->references==0u ||
       o->references==0xffffffffu)return false;
    ++o->references;return true;
}
inline void dereference(Object *o) {
    if(!o || !o->references)return;
    if(--o->references==0u) {
        // Clear type before callback so a stale pointer cannot be reopened.
        o->type=Type::none;
        auto fn=o->destroy;o->destroy=nullptr;
        if(fn)fn(o);
    }
}
template<unsigned CAPACITY=64u>
struct Handles {
    static_assert(CAPACITY>0u && CAPACITY<=1024u);
    struct Entry { Object *object=nullptr;uint32_t token=0u; };
    Entry slots[CAPACITY]{};
    uint32_t next_cookie=1u;
    unsigned used=0u,peak=0u,invalid=0u;
    // Identifiers occupy the positive int32 range and cannot be SDK fds.
    // Never reset next_cookie during process teardown or table reuse.
    Status open(Object *object,uint32_t *out) {
        if(!out || !object)return INVALID_PARAMETER;
        *out=0u;
        if(next_cookie>0x3fffffffu)return INSUFFICIENT_RESOURCES;
        unsigned i=0u;
        while(i<CAPACITY && slots[i].object)++i;
        if(i==CAPACITY)return INSUFFICIENT_RESOURCES;
        if(!reference(object))return INVALID_PARAMETER;
        const uint32_t token=0x40000000u|next_cookie++;
        slots[i]={object,token};
        *out=token;
        if(++used>peak)peak=used;
        return SUCCESS;
    }
    Status get(uint32_t handle,Type type,Object **out) {
        if(!out)return INVALID_PARAMETER;
        *out=nullptr;
        if((handle&0xC0000000u)!=0x40000000u)return INVALID_HANDLE;
        for(unsigned i=0u;i<CAPACITY;++i) {
            Entry &e=slots[i];
            if(e.token!=handle || !e.object)continue;
            if(type!=Type::none && e.object->type!=type)
                return OBJECT_TYPE_MISMATCH;
            *out=e.object;return SUCCESS;
        }
        ++invalid;
        return INVALID_HANDLE;
    }
    Status close(uint32_t handle,Type type=Type::none) {
        Object *o=nullptr;
        const Status st=get(handle,type,&o);
        if(st!=SUCCESS)return st;
        for(unsigned i=0u;i<CAPACITY;++i)
            if(slots[i].token==handle && slots[i].object==o) {
                slots[i]={};
                --used;
                dereference(o);
                return SUCCESS;
            }
        return INVALID_HANDLE;
    }
    void close_all() {
        for(unsigned i=0u;i<CAPACITY;++i) {
            Object *o=slots[i].object;
            slots[i]={};
            if(o)dereference(o);
        }
        used=0u;
    }
};
} // namespace nt
