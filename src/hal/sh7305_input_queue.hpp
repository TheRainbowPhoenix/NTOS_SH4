// SH7305 keyboard/touch IRP backend staging, P1-only, no SDK/MMIO.
// IRQ producer may call push ONLY while interrupts are masked and only
// after the native controller has been separately verified on hardware.
// This does not claim a native SH7305 input interrupt implementation.
#pragma once
#include <stdint.h>
#include "../nt/io_manager.hpp"
namespace ntos_hal {
enum class InputKind : uint32_t {keyboard=1u,touch=2u};
struct InputEvent {
    uint32_t kind=0u,key=0u,x=0u,y=0u;
};
static_assert(sizeof(InputEvent)==16u);
template<unsigned COUNT=16u>
struct InputQueue {
    static_assert(COUNT>=2u && COUNT<=64u);
    InputEvent slots[COUNT]{};
    unsigned head=0u,tail=0u,count=0u;
    uint32_t dropped=0u,pushed=0u,read=0u;
    bool push(InputEvent event) {
        if((event.kind!=1u && event.kind!=2u) ||
           (event.kind==1u && (event.x || event.y)) ||
           (event.kind==2u && event.key) ||
           event.x>=0x80000000u || event.y>=0x80000000u)
            return false;
        if(count==COUNT) {
            ++dropped;return false; // never overwrite unread events
        }
        slots[tail]=event;tail=(tail+1u)%COUNT;
        ++count;++pushed;return true;
    }
    bool pop(InputKind kind,InputEvent *out) {
        if(!out || !count)return false;
        // Preserve original ordering of events. A caller requesting the
        // other kind cannot skip or corrupt the head of a shared queue.
        if(slots[head].kind!=static_cast<uint32_t>(kind))return false;
        *out=slots[head];slots[head]={};
        head=(head+1u)%COUNT;--count;++read;return true;
    }
    static nt::Status transfer(void *opaque,Device device,
           nt::io::Major op,void *buffer,uint32_t bytes,uint32_t code,
           uint32_t *done) {
        if(!opaque || !done)return nt::INVALID_PARAMETER;
        *done=0u;
        const InputKind kind=device==Device::keyboard?InputKind::keyboard:
            device==Device::touchscreen?InputKind::touch:InputKind{};
        if(kind==InputKind{})return nt::NOT_SUPPORTED;
        if(op!=nt::io::Major::read || code ||
           bytes!=sizeof(InputEvent) || !buffer)
            return nt::INVALID_PARAMETER;
        InputEvent result{};
        if(!static_cast<InputQueue *>(opaque)->pop(kind,&result))
            return nt::DEVICE_BUSY; // nonblocking; no OS/input SDK call
        *static_cast<InputEvent *>(buffer)=result;
        *done=sizeof(InputEvent);
        return nt::SUCCESS;
    }
};
} // namespace ntos_hal
