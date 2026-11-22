#include "../src/hal/sh7305_adapter.hpp"
#include <cassert>
#include <cstdio>
struct Mock {
    uint8_t pattern=0x33u;
    unsigned seen=0u;
    static nt::Status transfer(void *opaque,ntos_hal::Device kind,
                               nt::io::Major major,void *buffer,
                               uint32_t bytes,uint32_t code,uint32_t *out) {
        auto &m=*static_cast<Mock *>(opaque);
        ++m.seen;
        if(kind!=ntos_hal::Device::keyboard ||
           major!=nt::io::Major::read || bytes!=1u ||
           code!=0u)return nt::NOT_SUPPORTED;
        *static_cast<uint8_t *>(buffer)=m.pattern;
        *out=1u;return nt::SUCCESS;
    }
    static bool buffer_check(void *,const void *buf,uint32_t bytes,bool) {
        return buf && bytes==1u;
    }
};
int main() {
    using namespace nt::io;
    Manager<2,2> manager{};
    Mock hw{},replacement{};replacement.pattern=0xABu;
    ntos_hal::Adapter keyboard{ntos_hal::Device::keyboard,
                               &hw,&Mock::transfer};
    ntos_hal::Adapter replacement_adapter{ntos_hal::Device::keyboard,
                                           &replacement,&Mock::transfer};
    DRIVER_OBJECT first=keyboard.driver(),second=replacement_adapter.driver();
    DEVICE_OBJECT *device=nullptr;
    assert(manager.IoCreateDevice("\\Device\\Keyboard",&first,&device)==nt::SUCCESS);
    uint32_t h=0u;
    assert(manager.open(257u,"\\device\\keyboard",&h)==nt::SUCCESS);
    uint8_t key=0u;IoStatusBlock io{};
    assert(manager.request(257u,h,Major::read,&key,1u,0u,
                           &io,&Mock::buffer_check,nullptr)==nt::SUCCESS);
    assert(key==0x33u && io.information==1u && keyboard.requests==1u);
    assert(manager.IoReplaceDriver(*device,&second)==nt::DEVICE_BUSY);
    assert(manager.close(257u,h)==nt::SUCCESS);
    assert(manager.IoReplaceDriver(*device,&second)==nt::SUCCESS);
    assert(manager.open(258u,"\\device\\keyboard",&h)==nt::SUCCESS);
    key=0u;
    assert(manager.request(257u,h,Major::read,&key,1u,0u,
                           &io,&Mock::buffer_check,nullptr)==nt::INVALID_HANDLE);
    assert(manager.request(258u,h,Major::read,&key,1u,0u,
                           &io,&Mock::buffer_check,nullptr)==nt::SUCCESS);
    assert(key==0xABu && replacement_adapter.requests==1u);
    assert(manager.request(258u,h,Major::read,&key,2u,0u,
                           &io,&Mock::buffer_check,nullptr)==nt::INVALID_PARAMETER);
    assert(manager.close_owner(258u)==nt::SUCCESS);
    assert(manager.IoDeleteDevice(*device)==nt::SUCCESS);
    std::puts("PASS SH7305 HAL adapter: keyboard mock, two replaceable drivers, no P0/SDK use in VBR, PID-scoped handles");
}
