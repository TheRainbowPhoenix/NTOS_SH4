#include "../src/nt/io_manager.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
struct Device {
    unsigned open=0u,closed=0u,calls=0u;
    uint8_t data[64]{};
    unsigned stored=0u;
    bool is_null=false;
    static nt::Status dispatch(void *raw,nt::io::DEVICE_OBJECT &,nt::io::IRP &irp) {
        auto &d=*static_cast<Device *>(raw);++d.calls;
        using namespace nt::io;
        switch(irp.major) {
            case Major::create:++d.open;break;
            case Major::close:++d.closed;break;
            case Major::write: {
                if(!d.is_null) {
                    if(irp.length>sizeof(d.data))return nt::INVALID_PARAMETER;
                    auto *p=static_cast<const uint8_t *>(irp.buffer);
                    for(unsigned i=0u;i<irp.length;++i)d.data[i]=p[i];
                    d.stored=irp.length;
                }
                return IoCompleteRequest(irp,nt::SUCCESS,irp.length)==nt::SUCCESS ?
                       nt::SUCCESS:nt::INTERNAL_ERROR;
            }
            case Major::read: {
                unsigned n=d.is_null?0u:d.stored;
                if(n>irp.length)n=irp.length;
                auto *p=static_cast<uint8_t *>(irp.buffer);
                for(unsigned i=0u;i<n;++i)p[i]=d.data[i];
                return IoCompleteRequest(irp,nt::SUCCESS,n)==nt::SUCCESS ?
                       nt::SUCCESS:nt::INTERNAL_ERROR;
            }
            case Major::device_control:
                return IoCompleteRequest(irp,nt::NOT_SUPPORTED,0u)==nt::SUCCESS ?
                       nt::NOT_SUPPORTED:nt::INTERNAL_ERROR;
        }
        return IoCompleteRequest(irp,nt::SUCCESS,0u)==nt::SUCCESS ?
               nt::SUCCESS:nt::INTERNAL_ERROR;
    }
    static bool check(void *,const void *p,uint32_t count,bool) {
        return p!=nullptr && count<=64u;
    }
};
int main() {
    using namespace nt::io;
    Manager<3,3> io{};
    Device null{},loop{};null.is_null=true;
    DRIVER_OBJECT nd{&Device::dispatch,&null};
    DRIVER_OBJECT ld{&Device::dispatch,&loop};
    DEVICE_OBJECT *dn=nullptr,*dl=nullptr;
    assert(io.IoCreateDevice("\\Device\\Null",&nd,&dn)==nt::SUCCESS);
    assert(io.IoCreateDevice("\\Device\\Loopback",&ld,&dl)==nt::SUCCESS);
    assert(io.IoCreateDevice("\\device\\null",&nd,&dn)==nt::CONFLICTING_ADDRESSES);
    uint32_t hnull=0u,hloop=0u;
    assert(io.open(257u,"\\DEVICE\\NULL",&hnull)==nt::SUCCESS);
    assert(io.open(257u,"\\Device\\Loopback",&hloop)==nt::SUCCESS);
    assert(io.IoReplaceDriver(*dl,&nd)==nt::DEVICE_BUSY);
    uint8_t tx[5]{1u,2u,3u,4u,5u},rx[5]{};
    IoStatusBlock ios{};
    assert(io.request(257u,hnull,Major::write,tx,5u,0u,&ios,&Device::check,nullptr)==nt::SUCCESS);
    assert(ios.information==5u);
    assert(io.request(257u,hnull,Major::read,rx,5u,0u,&ios,&Device::check,nullptr)==nt::SUCCESS);
    assert(ios.information==0u);
    assert(io.request(257u,hloop,Major::write,tx,5u,0u,&ios,&Device::check,nullptr)==nt::SUCCESS);
    assert(io.request(257u,hloop,Major::read,rx,5u,0u,&ios,&Device::check,nullptr)==nt::SUCCESS);
    assert(ios.information==5u && rx[4]==5u);
    assert(io.request(258u,hloop,Major::read,rx,5u,0u,&ios,&Device::check,nullptr)==nt::INVALID_HANDLE);
    assert(io.request(257u,hloop,Major::read,rx,5u,0u,&ios,nullptr,nullptr)==nt::INVALID_PARAMETER);
    assert(io.request(257u,hloop,Major::read,rx,4097u,0u,&ios,&Device::check,nullptr)==nt::INVALID_PARAMETER);
    assert(io.close_owner(257u)==nt::SUCCESS && io.open_count==0u);
    assert(io.close(257u,hloop)==nt::INVALID_HANDLE);
    assert(io.IoReplaceDriver(*dl,&nd)==nt::SUCCESS);
    assert(io.IoDeleteDevice(*dl)==nt::SUCCESS);
    IRP pending{};pending.major=Major::device_control;pending.pending=true;
    assert(IoCancelIrp(pending)==nt::SUCCESS);
    assert(IoCompleteRequest(pending,nt::SUCCESS,0u)==nt::INVALID_PARAMETER);
    assert(IoCompleteRequest(pending,CANCELLED,0u)==nt::SUCCESS);
    assert(IoCompleteRequest(pending,CANCELLED,0u)==nt::INVALID_PARAMETER);
    assert(loop.open==1u && loop.closed==1u);
    std::puts("PASS NT Io: driver/device/file/IRP, sync IO, PID isolation, stale handles, adapter swap, cancellation");
}
