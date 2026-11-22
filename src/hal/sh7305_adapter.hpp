// Pluggable SH7305 HAL adapters for NTOS Io. No hardware register access
// in this header: the board-specific backend supplies validated callbacks.
// Null/mock/host-SDK implementations can be swapped before native drivers.
// HAL/IO calls run in a normal Ke service, NEVER on a VBR/IRQ stack.
#pragma once
#include <stdint.h>
#include "../nt/io_manager.hpp"
namespace ntos_hal {
enum class Device : uint8_t {
    display=0u,keyboard=1u,touchscreen=2u,usb=3u
};
using Transfer=nt::Status (*)(void *,Device,nt::io::Major,
                              void *,uint32_t,uint32_t,uint32_t *);
struct Adapter {
    Device kind=Device::display;
    void *context=nullptr;
    Transfer transfer=nullptr;
    unsigned requests=0u,failures=0u;
    nt::io::DRIVER_OBJECT driver() {
        return {&Adapter::dispatch,this};
    }
    static nt::Status dispatch(void *opaque,nt::io::DEVICE_OBJECT &,
                                nt::io::IRP &irp) {
        auto &a=*static_cast<Adapter *>(opaque);
        using namespace nt::io;
        if(irp.major==Major::create || irp.major==Major::close) {
            const nt::Status s=IoCompleteRequest(irp,nt::SUCCESS,0u);
            return s==nt::SUCCESS ? nt::SUCCESS : nt::INTERNAL_ERROR;
        }
        if(!a.transfer) {
            ++a.failures;
            (void)IoCompleteRequest(irp,nt::NOT_SUPPORTED,0u);
            return nt::NOT_SUPPORTED;
        }
        ++a.requests;
        uint32_t done=0u;
        const nt::Status result=a.transfer(
            a.context,a.kind,irp.major,irp.buffer,irp.length,
            irp.control_code,&done);
        // Reject a misbehaving hardware/SDK adapter BEFORE publishing
        // a bogus byte count or corrupting IRP accounting.
        if(done>irp.length) {
            ++a.failures;
            (void)IoCompleteRequest(irp,nt::INTERNAL_ERROR,0u);
            return nt::INTERNAL_ERROR;
        }
        if(result!=nt::SUCCESS)++a.failures;
        const nt::Status completed=IoCompleteRequest(irp,result,done);
        return completed==nt::SUCCESS?result:nt::INTERNAL_ERROR;
    }
};
} // namespace ntos_hal
