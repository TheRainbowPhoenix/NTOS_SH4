/* Run the very same nt::Process used by the Exword guest API bridge. */
#include "../src/nt/process.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
static void *get(void *,size_t n) {return std::malloc(n);}
static void put(void *,void *p) {std::free(p);}
int main() {
    nt::Process p{};
    assert(p.begin());
    p.pool.configure(nullptr,get,put);
    uint32_t a=0u,b=0u;
    assert(p.create_event(true,false,&a)==nt::SUCCESS);
    assert(p.create_event(false,false,&b)==nt::SUCCESS);
    assert(a!=b && p.handles.used==2u);
    assert(p.event_poll(a,0u)==nt::TIMEOUT);
    assert(p.event_set(a)==nt::SUCCESS);
    assert(p.event_poll(a,0u)==nt::SUCCESS);
    assert(p.event_poll(a,0u)==nt::SUCCESS);
    assert(p.event_reset(a)==nt::SUCCESS);
    assert(p.event_poll(a,0u)==nt::TIMEOUT);
    assert(p.event_set(b)==nt::SUCCESS);
    assert(p.event_poll(b,0u)==nt::SUCCESS);
    assert(p.event_poll(b,0u)==nt::TIMEOUT);
    assert(p.event_poll(b,7u)==nt::NOT_SUPPORTED);
    assert(p.handles.close(a,nt::Type::event)==nt::SUCCESS);
    assert(p.handles.close(a,nt::Type::event)==nt::INVALID_HANDLE);
    uint32_t c=0u;
    assert(p.create_event(true,true,&c)==nt::SUCCESS);
    assert(c!=a);
    assert(p.handles.used==2u);
    assert(p.event_set(a)==nt::INVALID_HANDLE);
    const uint32_t tag=0x4c4f4f50u;
    void *slots[128]{};
    for(unsigned i=0;i<128u;++i) {
        slots[i]=p.pool.alloc(i+1,tag);
        assert(slots[i]);
    }
    assert(!p.pool.alloc(1,tag));
    assert(p.pool.current==(128u*129u)/2u);
    assert(p.pool.free(slots[127],tag^1u)==nt::INVALID_PARAMETER);
    for(unsigned i=0;i<128u;++i) {
        assert(p.pool.free(slots[i],tag)==nt::SUCCESS);
        assert(p.pool.free(slots[i],tag)==nt::INVALID_PARAMETER);
    }
    assert(p.pool.current==0u);
    assert(p.attach_file(7,&a)==nt::SUCCESS);
    assert(p.handles.used==3u);
    assert(p.file_fd(a)==7 && p.file_fd(c)==-1);
    assert(p.handles.close(a,nt::Type::event)==nt::OBJECT_TYPE_MISMATCH);
    assert(p.attach_file(8,&b)==nt::DEVICE_BUSY);
    assert(p.drop_file(a)==nt::SUCCESS);
    assert(p.file_fd(a)==-1);
    assert(p.handles.close(b,nt::Type::event)==nt::SUCCESS);
    assert(p.handles.close(c,nt::Type::event)==nt::SUCCESS);
    assert(p.handles.used==0u);
    p.finish();
    assert(!p.active);
    std::puts("PASS Exword NTOS Ob/Ex event, tagged pool, 128 slots, file cookies");
}
