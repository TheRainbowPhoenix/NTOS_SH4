// WinCE virtual address-space *planner*: no privileged MMU writes.
// The MMU, VBR, page fault handlers and OS restoration are not yet built.
#pragma once
#include <stdint.h>
#include "ce_pe.hpp"
namespace ceworld {
struct Mapping { uint32_t guest_va,physical,length,protection; };
inline bool plan_image(const cepe::Image &im,uint32_t physical,Mapping *out) {
 if(!out || (im.preferred&4095u)||(physical&4095u))return false;
 uint32_t length=(im.size+4095u)&~4095u;
 if(!length || length>12u*1024u*1024u||
    physical+length<physical)return false;
 *out={im.preferred,physical,length,7};
 return true;
}
} // namespace ceworld
