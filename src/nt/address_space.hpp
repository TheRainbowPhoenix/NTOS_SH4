// Current KPROCESS-owned VM root: each PID resolves only its own frames.
// NO ASID switch, no simultaneous process execution in this stage.
#pragma once
#include <stdint.h>
#include "virtual_memory.hpp"
namespace nt {
struct AddressSpaceRoot {
    VirtualMemory<64,8> *vm=nullptr;
    const uint32_t *current_pid=nullptr;
    uint32_t owner_pid=0u,epoch=0u;
    bool active=false;
    // An inactive process may prepare a private root without making it
    // current. The scheduler activates it only AFTER verified TLB removal.
    bool prepare(uint32_t pid,const uint32_t *live_pid,
                 VirtualMemory<64,8> *backing) {
        if(active || !pid || !live_pid ||
           !backing || !backing->backing || !backing->physical ||
           epoch==0xffffffffu)return false;
        vm=backing;current_pid=live_pid;owner_pid=pid;
        active=true;++epoch;return true;
    }
    bool bind(uint32_t pid,const uint32_t *live_pid,
              VirtualMemory<64,8> *backing) {
        return live_pid && *live_pid==pid &&
               prepare(pid,live_pid,backing);
    }
    bool valid() const {
        return active && vm && current_pid &&
               owner_pid && *current_pid==owner_pid;
    }
    bool resolve(uint32_t va,uint32_t *pa,ntos_mm::Access *rights) const {
        return valid() && vm->resolve(va,pa,rights);
    }
    void unbind() {
        // Only AFTER AT off and all guest translations flushed.
        vm=nullptr;current_pid=nullptr;owner_pid=0u;active=false;
    }
};
} // namespace nt
