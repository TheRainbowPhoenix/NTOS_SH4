// Host-only P1 keyboard/touch queues: bounded producer, ordered reader,
// nonblocking, real NT IRP adapter and per-PID Io file ownership.
#include "../src/hal/sh7305_input_queue.hpp"
#include "../src/hal/sh7305_adapter.hpp"
#include <cassert>
#include <cstdio>
static bool allow(void *,const void *p,uint32_t n,bool) {
    return p && n==sizeof(ntos_hal::InputEvent);
}
int main() {
    using namespace ntos_hal;
    using namespace nt::io;
    InputQueue<4u> queue{};
    Adapter keyboard{Device::keyboard,&queue,&InputQueue<4u>::transfer};
    Adapter touch{Device::touchscreen,&queue,&InputQueue<4u>::transfer};
    auto kdrv=keyboard.driver(),tdrv=touch.driver();
    Manager<2u,4u> io{};
    DEVICE_OBJECT *k=nullptr,*t=nullptr;
    assert(io.IoCreateDevice("\\Device\\Keyboard",&kdrv,&k)==nt::SUCCESS);
    assert(io.IoCreateDevice("\\Device\\Touch",&tdrv,&t)==nt::SUCCESS);
    uint32_t hk=0u,ht=0u;
    assert(io.open(0x101u,"\\Device\\Keyboard",&hk)==nt::SUCCESS);
    assert(io.open(0x102u,"\\Device\\Touch",&ht)==nt::SUCCESS);
    InputEvent e{};
    IoStatusBlock result{};
    assert(io.request(0x101u,hk,Major::read,&e,sizeof e,0u,
                      &result,&allow,nullptr)==nt::DEVICE_BUSY);
    assert(queue.push({1u,0xAAu,0u,0u}));
    assert(queue.push({2u,0u,20u,30u}));
    assert(queue.push({1u,0xBBu,0u,0u}));
    assert(queue.push({2u,0u,21u,31u}));
    assert(!queue.push({1u,0xCCu,0u,0u}) && queue.dropped==1u);
    assert(!queue.push({2u,1u,0u,0u}));
    assert(!queue.push({1u,0x1u,20u,30u}));
    assert(io.request(0x102u,ht,Major::read,&e,sizeof e,0u,
                      &result,&allow,nullptr)==nt::DEVICE_BUSY);
    assert(io.request(0x101u,hk,Major::read,&e,sizeof e,0u,
                      &result,&allow,nullptr)==nt::SUCCESS);
    assert(e.kind==1u && e.key==0xAAu && result.information==16u);
    assert(io.request(0x102u,ht,Major::read,&e,sizeof e,0u,
                      &result,&allow,nullptr)==nt::SUCCESS);
    assert(e.kind==2u && e.x==20u && e.y==30u);
    assert(io.request(0x101u,hk,Major::read,&e,sizeof e,0u,
                      &result,&allow,nullptr)==nt::SUCCESS);
    assert(e.kind==1u && e.key==0xBBu);
    assert(io.request(0x102u,ht,Major::read,&e,sizeof e,0u,
                      &result,&allow,nullptr)==nt::SUCCESS);
    assert(e.kind==2u && e.x==21u);
    assert(queue.count==0u && queue.read==4u);
    assert(io.request(0x102u,hk,Major::read,&e,sizeof e,0u,
                      &result,&allow,nullptr)==nt::INVALID_HANDLE);
    assert(io.request(0x101u,hk,Major::read,&e,4u,0u,
                      &result,&allow,nullptr)==nt::INVALID_PARAMETER);
    assert(io.close_owner(0x102u)==nt::SUCCESS && io.open_count==1u);
    assert(io.close_owner(0x101u)==nt::SUCCESS && io.open_count==0u);
    assert(io.IoDeleteDevice(*k)==nt::SUCCESS);
    assert(io.IoDeleteDevice(*t)==nt::SUCCESS);
    std::puts("PASS Hal input host: bounded keyboard/touch FIFO, deny cross-PID handles, validate IRP bytes, nonblocking empty, clean close");
}
