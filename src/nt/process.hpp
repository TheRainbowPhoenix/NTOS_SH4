// ps: lightweight per-launch resource ownership (NOT process isolation).
// Handle and ExPool teardown occurs only AFTER SDK descriptor recovery and
// AFTER the loader restores host MMU/VBR state.
#pragma once
#include "event.hpp"
#include "pool.hpp"
#include "virtual_memory.hpp"
namespace nt {
struct Process {
    Handles<64> handles{};
    Events<16> events{};
    Pool<128> pool{};
    VirtualMemory<64,8> vm{}; // 256 KiB arena, allocated by loader before VBR
    Object file{};
    Object finder{};
    int32_t raw_file=-1,raw_finder=-1;
    bool active=false;
    unsigned launches=0u;
    // The caller MUST have closed the previous host SDK resource.
    bool begin() {
        if(active || handles.used || raw_file>=0 || raw_finder>=0 ||
           pool.current || vm.reserved_pages || vm.committed_pages)return false;
        ++launches;active=true;return true;
    }
    void finish() {
        // Does NOT issue any SDK File_Close, File_Read or File_Open.
        handles.close_all();
        pool.release_all();
        // Called only AFTER MMU is disabled/UTLB flushed; no live aliases.
        vm.clear();
        raw_file=-1;raw_finder=-1;
        active=false;
    }
    Status create_event(bool manual,bool initial,uint32_t *handle) {
        if(!active)return DEVICE_BUSY;
        Event *e=events.create(manual,initial);
        if(!e)return INSUFFICIENT_RESOURCES;
        const Status result=handles.open(&e->header,handle);
        dereference(&e->header); // transfer creator's ref into handle
        return result;
    }
    Status event_set(uint32_t handle) {
        Object *o=nullptr;
        Status s=handles.get(handle,Type::event,&o);
        return s==SUCCESS?set_event(*reinterpret_cast<Event *>(o)):s;
    }
    Status event_reset(uint32_t handle) {
        Object *o=nullptr;
        Status s=handles.get(handle,Type::event,&o);
        return s==SUCCESS?reset_event(*reinterpret_cast<Event *>(o)):s;
    }
    Status event_poll(uint32_t handle,uint32_t timeout_ms) {
        Object *o=nullptr;
        Status s=handles.get(handle,Type::event,&o);
        return s==SUCCESS?poll_event(*reinterpret_cast<Event *>(o),timeout_ms):s;
    }
    Status attach_file(int32_t fd,uint32_t *handle) {
        if(!active || raw_file>=0 || fd<0)return DEVICE_BUSY;
        init(file,Type::file);
        const Status s=handles.open(&file,handle);
        dereference(&file);
        if(s==SUCCESS)raw_file=fd;
        return s;
    }
    Status attach_finder(int32_t fd,uint32_t *handle) {
        if(!active || raw_finder>=0 || fd<0)return DEVICE_BUSY;
        init(finder,Type::finder);
        const Status s=handles.open(&finder,handle);
        dereference(&finder);
        if(s==SUCCESS)raw_finder=fd;
        return s;
    }
    int32_t file_fd(uint32_t handle) {
        Object *o=nullptr;
        return handles.get(handle,Type::file,&o)==SUCCESS && o==&file ?
            raw_file : -1;
    }
    int32_t finder_fd(uint32_t handle) {
        Object *o=nullptr;
        return handles.get(handle,Type::finder,&o)==SUCCESS && o==&finder ?
            raw_finder : -1;
    }
    Status drop_file(uint32_t handle) {
        if(file_fd(handle)<0)return INVALID_HANDLE;
        const Status s=handles.close(handle,Type::file);
        if(s==SUCCESS)raw_file=-1;
        return s;
    }
    Status drop_finder(uint32_t handle) {
        if(finder_fd(handle)<0)return INVALID_HANDLE;
        const Status s=handles.close(handle,Type::finder);
        if(s==SUCCESS)raw_finder=-1;
        return s;
    }
};
} // namespace nt
