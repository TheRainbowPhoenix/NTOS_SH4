// pe_rebase.hpp - NTOS PE32 SH4BE mapped-module placement and fixups.
//
// PE metadata (.reloc entries) is little-endian; relocated SH4 pointers are
// big-endian. Keep imports/exports RVA-based until the distinct module bases
// are selected; snap IATs only AFTER rebasing each image.
//
// Supported relocation types: IMAGE_REL_BASED_ABSOLUTE (0, padding) and
// IMAGE_REL_BASED_HIGHLOW (3, full 32-bit big-endian word). Reject all other
// types rather than silently launching a corrupted relocated module.
#pragma once
#include <stdint.h>
#include "pe.hpp"

namespace ntos_rebase {
constexpr uint32_t PAGE = 0x1000u;
constexpr uint32_t VA_MIN = 0x00100000u;
constexpr uint32_t VA_MAX = 0x07FF0000u; // exclusive
constexpr uint32_t FALLBACK = 0x01000000u;
constexpr uint32_t API_BASE = 0x08100000u;
constexpr uint32_t API_PAGES = 4u;
constexpr unsigned SET_COUNT = 32u;
constexpr unsigned SET_WAYS = 4u;
struct Region { uint32_t base; uint32_t pages; };

inline bool overlap(uint32_t a, uint32_t apages,
                    uint32_t b, uint32_t bpages) {
    // All bases are validated as 4 KiB-aligned and within VA_MAX.
    return a < b + bpages * PAGE && b < a + apages * PAGE;
}
inline bool fits(uint32_t candidate, uint32_t pages,
                 const Region *used, unsigned nused) {
    if ((candidate & (PAGE-1u)) != 0u || candidate < VA_MIN ||
        pages == 0u || pages > 55u ||
        candidate >= VA_MAX || pages * PAGE > VA_MAX - candidate)
        return false;
    if (overlap(candidate,pages,API_BASE,API_PAGES))
        return false;
    unsigned occupancy[SET_COUNT] = {};
    // The API mapping consumes four rotating-index ways, one per set 0..3.
    for (uint32_t p=0; p<API_PAGES; ++p)
        ++occupancy[((API_BASE >> 12)+p)&(SET_COUNT-1u)];
    for (unsigned j=0; j<nused; ++j) {
        if (used[j].pages == 0u) continue;
        if (overlap(candidate,pages,used[j].base,used[j].pages))
            return false;
        for (uint32_t p=0; p<used[j].pages; ++p) {
            unsigned set=((used[j].base >> 12)+p)&(SET_COUNT-1u);
            if (++occupancy[set]>SET_WAYS) return false;
        }
    }
    for (uint32_t p=0; p<pages; ++p) {
        unsigned set=((candidate >> 12)+p)&(SET_COUNT-1u);
        if (++occupancy[set]>SET_WAYS) return false;
    }
    return true;
}
inline bool choose_base(uint32_t preferred, uint32_t pages,
                        const Region *used, unsigned nused,
                        uint32_t *selected) {
    if (!selected || nused>8u) return false;
    if (fits(preferred,pages,used,nused)) {
        *selected=preferred; return true;
    }
    // First-fit walk on 4 KiB boundaries, not 4 MiB increments: skewing
    // virtual-page set indices is sometimes necessary to avoid 4-way clashes.
    for (uint32_t va=FALLBACK; va<VA_MAX; va+=PAGE) {
        if (fits(va,pages,used,nused)) {
            *selected=va; return true;
        }
    }
    return false;
}
inline bool apply(const pe::Image &hdr,uint8_t *image,uint32_t image_size,
                  uint32_t actual_base,const char **error) {
    if (!image || !error) return false;
    *error="ok";
    uint32_t delta=actual_base-hdr.oh.image_base;
    if (delta==0u) return true;
    const uint32_t begin=hdr.oh.basereloc_rva;
    const uint32_t length=hdr.oh.basereloc_size;
    if (!begin || length<8u || begin>=image_size ||
        length>image_size-begin) {
        *error="rebasing requires valid PE .reloc directory";
        return false;
    }
    uint32_t offset=begin,end=begin+length;
    // Validate all blocks/sites/types before changing any bytes, making
    // a rejected relocated image unable to leave a half-applied fixup set.
    for (unsigned pass=0; pass<2u; ++pass) {
        offset=begin;
        while (offset<end) {
            if (end-offset<8u) {
                *error="truncated PE relocation block";return false;
            }
            uint32_t page=pe::rd32le(image+offset);
            uint32_t size=pe::rd32le(image+offset+4u);
            if (size<8u || (size&1u) || size>end-offset) {
                *error="invalid PE relocation block size";return false;
            }
            for (uint32_t i=8u;i<size;i+=2u) {
                uint16_t e=pe::rd16le(image+offset+i);
                uint32_t type=e>>12,ofs=e&0xfffu;
                if (type==pe::BRELOC_ABSOLUTE) continue;
                if (type!=pe::BRELOC_HIGHLOW) {
                    *error="unsupported PE base relocation type";return false;
                }
                if (page>image_size || ofs>image_size-page ||
                    4u>image_size-page-ofs) {
                    *error="PE relocation target outside loaded image";
                    return false;
                }
                if (pass==1u) {
                    uint8_t *at=image+page+ofs;
                    pe::wr32be(at,pe::rd32be(at)+delta);
                }
            }
            offset+=size;
        }
    }
    return true;
}
} // namespace ntos_rebase
