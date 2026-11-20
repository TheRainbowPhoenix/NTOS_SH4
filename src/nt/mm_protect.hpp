// Mm: PE32 page protection policy for SH7305 4 KiB UTLB mappings.
//
// Neptune-style separation: loader determines virtual-section rights,
// pager owns residency, HAL encodes rights into SH4A PTEL.PR.
// No filesystem, heap, SDK, callbacks, C++ runtime or P0 memory accesses.
// SH4A PTEL supports RO/RW but has NO execute-disable (NX) bit.
// A 4 KiB page overlapping a writable PE section must remain RW even if
// the same page also contains code/rodata (union-of-section permissions).
#pragma once
#include <stdint.h>
#include "../pe.hpp"

namespace ntos_mm {
enum class Access : uint8_t { no_access=0u, read_only=1u, read_write=2u };
constexpr uint32_t PAGE=4096u;
inline bool writable(Access a) { return a==Access::read_write; }
inline bool readable(Access a) { return a!=Access::no_access; }

// Applies to an already validated PE header (pe::parse / stream validation).
// Returns no_access for gaps beyond headers that do not intersect sections.
inline Access pe_page_access(const pe::Image &image,uint32_t page) {
    if(page>0xffffffffu/PAGE || !image.oh.size_of_image)
        return Access::no_access;
    const uint32_t start=page*PAGE;
    if(start>=image.oh.size_of_image)return Access::no_access;
    const uint32_t remaining=image.oh.size_of_image-start;
    const uint32_t end=start+(remaining<PAGE?remaining:PAGE);
    bool covered=start<image.oh.size_of_headers;
    bool write=false;
    for(uint32_t i=0u;i<image.section_count && i<8u;++i) {
        const pe::SectionHeader &sec=image.sections[i];
        const uint32_t bytes=sec.raw_size>sec.vsize ?
                             sec.raw_size:sec.vsize;
        if(bytes==0u || sec.rva>=image.oh.size_of_image ||
           bytes>image.oh.size_of_image-sec.rva)
            continue; // caller must separately reject malformed PE
        const uint32_t section_end=sec.rva+bytes;
        if(sec.rva<end && start<section_end) {
            covered=true;
            if((sec.characteristics&pe::SCN_MEM_WRITE)!=0u)
                write=true;
        }
    }
    return !covered ? Access::no_access :
           write ? Access::read_write : Access::read_only;
}
} // namespace ntos_mm
