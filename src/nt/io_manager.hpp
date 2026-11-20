// Tiny NT-style Io Manager: statically bounded DRIVER/DEVICE/FILE/IRP.
// No GUI, SDK, heap, VBR, or external dependency; HAL adapters are swappable.
// Guest buffers must pass the caller's P0 access validator before dispatch.
// Only synchronous device calls are published; deferred IRPs are future work.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ntstatus.hpp"

namespace nt::io {
constexpr Status PENDING=0x00000103u;
constexpr Status CANCELLED=0xC0000120u;
constexpr uint32_t MAX_TRANSFER=4096u;
enum class Major : uint8_t {
    create=0u,close=1u,read=2u,write=3u,device_control=4u
};
struct IoStatusBlock {
    Status status=NOT_SUPPORTED;
    uint32_t information=0u;
};
struct IRP {
    Major major=Major::create;
    void *buffer=nullptr;
    uint32_t length=0u;
    uint32_t control_code=0u;
    uint32_t process_id=0u;
    bool completed=false;
    bool pending=false;
    bool cancel_requested=false;
    IoStatusBlock io_status{};
};
struct DEVICE_OBJECT;
using Dispatch=Status (*)(void *,DEVICE_OBJECT &,IRP &);
struct DRIVER_OBJECT {
    Dispatch major_dispatch=nullptr;
    void *extension=nullptr;
};
struct DEVICE_OBJECT {
    char name[40]{};
    DRIVER_OBJECT *driver=nullptr;
    bool active=false;
};
struct FILE_OBJECT {
    DEVICE_OBJECT *device=nullptr;
    uint32_t owner_pid=0u,handle=0u;
};

inline Status IoCompleteRequest(IRP &irp,Status result,uint32_t bytes) {
    if(irp.completed || bytes>irp.length)return INVALID_PARAMETER;
    if(irp.cancel_requested && result!=CANCELLED)return INVALID_PARAMETER;
    irp.io_status={result,bytes};
    irp.completed=true;irp.pending=false;
    return SUCCESS;
}
inline Status IoCancelIrp(IRP &irp) {
    if(irp.completed || !irp.pending)return INVALID_PARAMETER;
    irp.cancel_requested=true;
    return SUCCESS; // driver must complete with CANCELLED
}
inline Status IoCallDriver(DEVICE_OBJECT &device,IRP &irp) {
    if(!device.active || !device.driver ||
       !device.driver->major_dispatch || irp.completed)
        return INVALID_PARAMETER;
    Status s=device.driver->major_dispatch(
        device.driver->extension,device,irp);
    if(s==PENDING) {
        if(irp.completed)return irp.io_status.status;
        irp.pending=true;return PENDING;
    }
    // A driver must complete exactly once; returned status and completion
    // status must agree, preventing silent success with lost data.
    if(!irp.completed || irp.io_status.status!=s)
        return INTERNAL_ERROR;
    return s;
}
inline bool same_name(const char *a,const char *b) {
    if(!a || !b)return false;
    for(unsigned i=0u;i<40u;++i) {
        char x=a[i],y=b[i];
        if(x>='a' && x<='z')x-=32;
        if(y>='a' && y<='z')y-=32;
        if(x!=y)return false;
        if(!x)return true;
    }
    return false;
}
using BufferCheck=bool (*)(void *,const void *,uint32_t,bool);
template<unsigned DEVICE_COUNT=4u,unsigned OPEN_COUNT=8u>
struct Manager {
    static_assert(DEVICE_COUNT>0u && DEVICE_COUNT<=16u);
    static_assert(OPEN_COUNT>0u && OPEN_COUNT<=64u);
    DEVICE_OBJECT devices[DEVICE_COUNT]{};
    FILE_OBJECT files[OPEN_COUNT]{};
    uint32_t next_handle=1u;
    unsigned open_count=0u;

    Status IoCreateDevice(const char *name,DRIVER_OBJECT *driver,
                          DEVICE_OBJECT **out) {
        if(!out)return INVALID_PARAMETER;
        *out=nullptr;
        if(!name || !driver || !driver->major_dispatch ||
           name[0]!='\\')return INVALID_PARAMETER;
        unsigned n=0u;
        for(;n<39u && name[n];++n) {}
        if(!n || n==39u || name[n])return INVALID_PARAMETER;
        for(const auto &d:devices)
            if(d.active && same_name(d.name,name))
                return CONFLICTING_ADDRESSES;
        for(auto &d:devices)if(!d.active) {
            for(unsigned i=0u;i<=n;++i)d.name[i]=name[i];
            d.driver=driver;d.active=true;*out=&d;
            return SUCCESS;
        }
        return INSUFFICIENT_RESOURCES;
    }
    Status IoReplaceDriver(DEVICE_OBJECT &device,DRIVER_OBJECT *replacement) {
        if(!device.active || !replacement ||
           !replacement->major_dispatch)return INVALID_PARAMETER;
        for(const auto &f:files)if(f.device==&device)return DEVICE_BUSY;
        device.driver=replacement;return SUCCESS;
    }
    Status IoDeleteDevice(DEVICE_OBJECT &device) {
        if(!device.active)return INVALID_PARAMETER;
        for(const auto &f:files)if(f.device==&device)return DEVICE_BUSY;
        device=DEVICE_OBJECT{};return SUCCESS;
    }
    Status open(uint32_t pid,const char *name,uint32_t *out) {
        if(!out)return INVALID_PARAMETER;
        *out=0u;
        if(!pid || !name || next_handle>0x00FFFFFFu)
            return INVALID_PARAMETER;
        DEVICE_OBJECT *d=nullptr;
        for(auto &candidate:devices)
            if(candidate.active && same_name(candidate.name,name)) {
                d=&candidate;break;
            }
        if(!d)return INVALID_HANDLE;
        FILE_OBJECT *f=nullptr;
        for(auto &candidate:files)if(!candidate.device) {
            f=&candidate;break;
        }
        if(!f)return INSUFFICIENT_RESOURCES;
        IRP irp{};irp.major=Major::create;irp.process_id=pid;
        const Status status=IoCallDriver(*d,irp);
        if(status!=SUCCESS)return status==PENDING?NOT_SUPPORTED:status;
        const uint32_t token=0x50000000u|next_handle++;
        *f={d,pid,token};*out=token;++open_count;
        return SUCCESS;
    }
    FILE_OBJECT *lookup(uint32_t pid,uint32_t handle) {
        if(!pid || (handle&0xFF000000u)!=0x50000000u)
            return nullptr;
        for(auto &f:files)
            if(f.device && f.owner_pid==pid && f.handle==handle)
                return &f;
        return nullptr;
    }
    Status close(uint32_t pid,uint32_t handle) {
        FILE_OBJECT *f=lookup(pid,handle);
        if(!f)return INVALID_HANDLE;
        IRP irp{};irp.major=Major::close;irp.process_id=pid;
        const Status status=IoCallDriver(*f->device,irp);
        if(status!=SUCCESS)return status==PENDING?NOT_SUPPORTED:status;
        *f=FILE_OBJECT{};--open_count;return SUCCESS;
    }
    Status request(uint32_t pid,uint32_t handle,Major major,
                   void *buffer,uint32_t length,uint32_t control,
                   IoStatusBlock *out,BufferCheck check,
                   void *check_context) {
        if(!out)return INVALID_PARAMETER;
        *out={};
        FILE_OBJECT *f=lookup(pid,handle);
        if(!f)return INVALID_HANDLE;
        if(major!=Major::read && major!=Major::write &&
           major!=Major::device_control)return INVALID_PARAMETER;
        if(length>MAX_TRANSFER || (length && !buffer) ||
           (length && (!check ||
            !check(check_context,buffer,length,major!=Major::write))))
            return INVALID_PARAMETER;
        IRP irp{};
        irp.major=major;irp.buffer=buffer;irp.length=length;
        irp.control_code=control;irp.process_id=pid;
        const Status result=IoCallDriver(*f->device,irp);
        if(result==PENDING)return NOT_SUPPORTED; // cannot escape stack IRP
        if(irp.completed)*out=irp.io_status;
        return result;
    }
    // Caller owns host file cleanup; this closes ONLY kernel-created
    // device handles, not SDK descriptors, and never runs in VBR.
    Status close_owner(uint32_t pid) {
        if(!pid)return INVALID_PARAMETER;
        for(const auto &f:files)
            if(f.device && f.owner_pid==pid) {
                const Status s=close(pid,f.handle);
                if(s!=SUCCESS)return s;
            }
        return SUCCESS;
    }
};
} // namespace nt::io
