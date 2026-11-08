// pe_rebase.hpp - NTOS PE32 SH4BE mapped-module placement and fixups.
//
// PE metadata (.reloc entries) is little-endian; relocated SH4 pointers are
// big-endian. Keep imports/exports RVA-based until the distinct module bases
// are selected; snap IATs only AFTER rebasing each image.
//
// Supports ABSOLUTE(0), HIGH(1), LOW(2), HIGHLOW(3), HIGHADJ(4),
// including WinCE's paired HIGH/LOW logic. Rejects other records.
#pragma once
#include <stdint.h>
#include "pe.hpp"

namespace ntos_rebase {
constexpr uint32_t PAGE = 0x1000u;
constexpr uint32_t VA_MIN = 0x00010000u; // WinCE EXE preferred base
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
    if (!selected || !used || nused>32u) return false;
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
// NT 3.x / WinCE-compatible PE32 relocation primitives. The *headers*
// always use LE byte order; the target's instructions/data use their CPU
// byte order. Our on-device SH4A-DSP target is BE, while host tests can
// inspect the supplied WinCE SH4LE images without executing them.
inline uint16_t load16(const uint8_t *p,bool be) {
    return be ? static_cast<uint16_t>((uint32_t(p[0])<<8)|p[1])
              : pe::rd16le(p);
}
inline void store16(uint8_t *p,uint16_t v,bool be) {
    p[be?0:1]=static_cast<uint8_t>(v>>8);
    p[be?1:0]=static_cast<uint8_t>(v);
}
inline uint32_t load32(const uint8_t *p,bool be) {
    return be ? pe::rd32be(p) : pe::rd32le(p);
}
inline void store32(uint8_t *p,uint32_t v,bool be) {
    if (be) pe::wr32be(p,v);
    else {
        p[0]=static_cast<uint8_t>(v);
        p[1]=static_cast<uint8_t>(v>>8);
        p[2]=static_cast<uint8_t>(v>>16);
        p[3]=static_cast<uint8_t>(v>>24);
    }
}
// The two passes ensure that malformed relocation metadata never leaves
// the image partially modified. C++ arithmetic is unsigned modulo 2^32,
// matching PE HIGHLOW rebasing even for negative load deltas.
inline bool apply_with_endian(const pe::Image &hdr,uint8_t *image,
        uint32_t image_size,uint32_t actual_base,bool data_big_endian,
        const char **error) {
    if (!image || !error) return false;
    *error="ok";
    const uint32_t delta=actual_base-hdr.oh.image_base;
    if (!delta) return true;
    if ((hdr.fh.characteristics & pe::F_RELOCS_STRIPPED)!=0u) {
        *error="PE marks image fixed: relocations stripped";
        return false;
    }
    const uint32_t start=hdr.oh.basereloc_rva;
    const uint32_t length=hdr.oh.basereloc_size;
    if (!start || length<8u || start>=image_size ||
        length>image_size-start) {
        *error="rebasing requires valid PE .reloc directory";
        return false;
    }
    const uint32_t end=start+length;
    for (unsigned pass=0;pass<2u;++pass) {
        uint32_t pos=start;
        // WinCE SH relocations may use HIGH followed by LOW for a
        // split absolute address. HIGHADJ consumes its following WORD.
        uint32_t pending_high=0;
        bool has_high=false;
        auto finish_high=[&]() {
            if (has_high && pass==1u) {
                uint8_t *at=image+pending_high;
                store16(at,static_cast<uint16_t>(
                    load16(at,data_big_endian)+(delta>>16)),
                    data_big_endian);
            }
            has_high=false;
        };
        while(pos<end) {
            if(end-pos<8u) {
                *error="truncated PE relocation block";return false;
            }
            const uint32_t page=pe::rd32le(image+pos);
            const uint32_t block=pe::rd32le(image+pos+4u);
            if(block<8u || (block&3u) || block>end-pos ||
               (page&(PAGE-1u))) {
                *error="invalid PE relocation page/block";return false;
            }
            for(uint32_t off=8u;off<block;off+=2u) {
                const uint16_t entry=pe::rd16le(image+pos+off);
                const uint32_t type=entry>>12;
                if(type==0u) continue;  // ABSOLUTE padding
                const uint32_t site=page+(entry&0xfffu);
                const uint32_t needed=(type==3u)?4u:2u;
                if (type!=1u && type!=2u && type!=3u && type!=4u) {
                    *error="unsupported SH4 PE relocation type";return false;
                }
                if(page>image_size || site<page ||
                   site>image_size || needed>image_size-site) {
                    *error="PE relocation target outside loaded image";
                    return false;
                }
                // Never allow a fixup to modify yet-unprocessed .reloc
                // descriptors during the second validation/write pass.
                if(site<end && start<site+needed) {
                    *error="PE fixup overlaps relocation directory";
                    return false;
                }
                uint16_t adj=0;
                if(type==4u) {
                    if(off+4u>block) {
                        *error="truncated PE HIGHADJ addend";return false;
                    }
                    adj=pe::rd16le(image+pos+off+2u);
                    off+=2u;
                }
                if(pass==0u) continue;
                uint8_t *at=image+site;
                if(type==1u) {
                    finish_high();
                    pending_high=site;
                    has_high=true;
                } else if(type==2u && has_high) {
                    uint8_t *hi=image+pending_high;
                    // From CE NK/KERNEL/loader.c RelocatePage():
                    // full = (HI<<16) + LOW + delta, with rounded HI.
                    uint32_t v=(uint32_t(load16(hi,data_big_endian))<<16)
                               +load16(at,data_big_endian)+delta;
                    store16(hi,static_cast<uint16_t>((v+0x8000u)>>16),
                            data_big_endian);
                    store16(at,static_cast<uint16_t>(v),data_big_endian);
                    has_high=false;
                } else {
                    finish_high();
                    if(type==2u) {
                        store16(at,static_cast<uint16_t>(
                                load16(at,data_big_endian)+delta),
                                data_big_endian);
                    } else if(type==3u) {
                        store32(at,load32(at,data_big_endian)+delta,
                                data_big_endian);
                    } else if(type==4u) {
                        const int32_t signed_low=
                            static_cast<int16_t>(adj);
                        const uint32_t increment=
                            (delta+static_cast<uint32_t>(signed_low)+
                             0x8000u)>>16;
                        store16(at,static_cast<uint16_t>(
                            load16(at,data_big_endian)+increment),
                            data_big_endian);
                    }
                }
            }
            pos+=block;
        }
        finish_high();
    }
    return true;
}
inline bool apply(const pe::Image &hdr,uint8_t *image,uint32_t image_size,
                  uint32_t actual_base,const char **error) {
    return apply_with_endian(hdr,image,image_size,actual_base,true,error);
}

} // namespace ntos_rebase
