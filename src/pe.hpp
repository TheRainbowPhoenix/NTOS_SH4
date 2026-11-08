// pe.hpp -- standard PE32 image parsing for big-endian SH4 payloads.
//
// Payload files are real PE executables (.EXE/.DLL): MZ header, "PE\0\0" signature,
// little-endian header fields and directory tables, exactly as pefile /
// dumpbin / Ghidra's PE loader expect -- the machine type 0x01A8 marks the
// code as SH4 big-endian. Only what the *file format* defines is
// little-endian; the bytes the SH4 BE CPU itself reads (code, literal
// pools, .data pointers, the IAT slots after snapping) are big-endian in
// memory. The loader therefore parses with the LE readers below and patches
// payload-visible words with the BE writers.
//
// The load model mirrors WinCE's nk.exe (NK/KERNEL/LOADER.C in the
// WinCE PRIVATE source tree): copy-load + base relocation for
// position-independent images, or XIP-style mapping at the preferred base
// with an import snap against a mapped COREDLL -- done here over the HH3
// heap + UTLB.
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace pe {

// No assigned PE machine type exists for SH4 big-endian. Microsoft uses
// 0x01A6 for SH4 (normally LE) and 0x01A8 for SH5.
// 0x01A7 is reserved by NTOS for the experimental native SH4BE PE target.
constexpr uint16_t MACHINE_SH4BE_NATIVE = 0x01A7;
constexpr uint16_t MACHINE_SH4BE_LEGACY = 0x01A8;
constexpr uint16_t MACHINE_SH4BE = MACHINE_SH4BE_LEGACY;
constexpr uint16_t MACHINE_SH4LE = 0x01A6;  // MS standard SH4 (LE)

constexpr uint16_t OPT_MAGIC_PE32 = 0x010B;
constexpr uint16_t SUBSYSTEM_WINDOWS_CE = 9;

// File header characteristics
constexpr uint16_t F_RELOCS_STRIPPED  = 0x0001; // fixed at preferred base
constexpr uint16_t F_EXECUTABLE_IMAGE = 0x0002;
constexpr uint16_t F_32BIT_MACHINE    = 0x0104;
constexpr uint16_t F_DLL              = 0x2000;

// Section characteristics
constexpr uint32_t SCN_CNT_CODE               = 0x00000020;
constexpr uint32_t SCN_CNT_INITIALIZED_DATA   = 0x00000040;
constexpr uint32_t SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
constexpr uint32_t SCN_MEM_DISCARDABLE        = 0x02000000;
constexpr uint32_t SCN_MEM_EXECUTE            = 0x20000000;
constexpr uint32_t SCN_MEM_READ               = 0x40000000;
constexpr uint32_t SCN_MEM_WRITE              = 0x80000000;

// PE32 optional header data directory indices
constexpr int DATADIR_EXPORT    = 0;
constexpr int DATADIR_IMPORT    = 1;
constexpr int DATADIR_BASERELOC = 5;

// Base relocation types
constexpr uint8_t BRELOC_ABSOLUTE = 0;
constexpr uint8_t BRELOC_HIGHLOW  = 3;

// --- byte readers -----------------------------------------------------------
// Little-endian: everything the PE file format defines.
inline uint16_t rd16le(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t rd32le(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
// Big-endian: payload-visible memory words (SH4 BE loads/stores).
inline uint32_t rd32be(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}
inline void wr32be(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

struct FileHeader {
    uint16_t machine;
    uint16_t nsections;
    uint32_t timestamp;
    uint32_t symtab_offset;
    uint32_t nsymbols;
    uint16_t opthdr_size;
    uint16_t characteristics;
};

struct OptHeader {
    uint16_t magic;         // 0x010B (PE32)
    uint32_t entry_rva;     // AddressOfEntryPoint
    uint32_t image_base;    // preferred load address
    uint32_t section_align;
    uint32_t file_align;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint16_t subsystem;
    // data directories (0 if absent)
    uint32_t export_rva, export_size;
    uint32_t import_rva, import_size;
    uint32_t basereloc_rva, basereloc_size;
};

struct SectionHeader {
    char     name[8];
    uint32_t vsize;
    uint32_t rva;
    uint32_t raw_size;
    uint32_t raw_offset;
    uint32_t characteristics;
};

struct Image {
    FileHeader  fh;
    OptHeader   oh;      // valid only if fh.opthdr_size >= 28
    uint32_t    section_count;
    SectionHeader sections[8];
    uint32_t    image_end;  // max(rva + max(vsize, raw_size)) across sections
};

// Parse the DOS header, PE signature, COFF file header, PE32 optional
// header and section table from `data`. Returns 0 on success, negative
// error code otherwise.
inline int parse(const uint8_t *data, uint32_t size, Image *img) {
    if(!data || !img || size < 0x40u ||
       data[0] != 'M' || data[1] != 'Z')
        return -1;
    const uint32_t lfanew = rd32le(data + 0x3C);
    if (lfanew > size || 24u > size - lfanew ||
        data[lfanew] != 'P' || data[lfanew + 1] != 'E' ||
        data[lfanew + 2] != 0 || data[lfanew + 3] != 0)
        return -1; // missing PE\0\0 signature

    const uint8_t *f = data + lfanew + 4;
    img->fh.machine         = rd16le(f + 0);
    img->fh.nsections       = rd16le(f + 2);
    img->fh.timestamp       = rd32le(f + 4);
    img->fh.symtab_offset   = rd32le(f + 8);
    img->fh.nsymbols        = rd32le(f + 12);
    img->fh.opthdr_size     = rd16le(f + 16);
    img->fh.characteristics = rd16le(f + 18);

    if (img->fh.machine != MACHINE_SH4BE_NATIVE &&
        img->fh.machine != MACHINE_SH4BE_LEGACY)
        return -2; // not a big-endian SH4 image
    if (img->fh.nsections == 0 || img->fh.nsections > 8)
        return -3;
    // Validate without ever constructing a potentially wrapped
    // 32-bit pointer or optional-header/section-table offset.
    const uint32_t after_file_header=lfanew+24u;
    const uint32_t section_bytes=
        static_cast<uint32_t>(img->fh.nsections)*40u;
    if (img->fh.opthdr_size<144u ||
        img->fh.opthdr_size>size-after_file_header ||
        section_bytes>size-after_file_header-img->fh.opthdr_size)
        return -4;

    if (img->fh.opthdr_size >= 28) {
        const uint8_t *o = f + 20;
        img->oh.magic          = rd16le(o + 0);
        img->oh.entry_rva      = rd32le(o + 16);
        img->oh.image_base     = rd32le(o + 28);
        img->oh.section_align  = (img->fh.opthdr_size >= 40) ? rd32le(o + 32) : 16;
        img->oh.file_align     = (img->fh.opthdr_size >= 40) ? rd32le(o + 36) : 16;
        img->oh.size_of_image  = (img->fh.opthdr_size >= 60) ? rd32le(o + 56) : 0;
        img->oh.size_of_headers= (img->fh.opthdr_size >= 64) ? rd32le(o + 60) : 0;
        img->oh.subsystem      = (img->fh.opthdr_size >= 70) ? rd16le(o + 68) : 0;
        // PE32 data directories start at optional-header offset 96:
        // #0 export at 96, #1 import at 104, #5 base reloc at 136.
        if (img->fh.opthdr_size >= 144) {
            img->oh.export_rva     = rd32le(o + 96);
            img->oh.export_size    = rd32le(o + 100);
            img->oh.import_rva     = rd32le(o + 104);
            img->oh.import_size    = rd32le(o + 108);
            img->oh.basereloc_rva  = rd32le(o + 136);
            img->oh.basereloc_size = rd32le(o + 140);
        } else {
            img->oh.export_rva     = 0;
            img->oh.export_size    = 0;
            img->oh.import_rva     = 0;
            img->oh.import_size    = 0;
            img->oh.basereloc_rva  = 0;
            img->oh.basereloc_size = 0;
        }
    } else {
        img->oh.magic = 0;
        img->oh.entry_rva = 0;
        img->oh.image_base = 0;
        img->oh.section_align = 0;
        img->oh.file_align = 0;
        img->oh.size_of_image = 0;
        img->oh.size_of_headers = 0;
        img->oh.subsystem = 0;
        img->oh.export_rva = 0;
        img->oh.export_size = 0;
        img->oh.import_rva = 0;
        img->oh.import_size = 0;
        img->oh.basereloc_rva = 0;
        img->oh.basereloc_size = 0;
    }

    const uint8_t *s = f + 20 + img->fh.opthdr_size;
    img->section_count = img->fh.nsections;
    img->image_end = 0;
    for (uint32_t i = 0; i < img->section_count; i++) {
        SectionHeader &sh = img->sections[i];
        for (int k = 0; k < 8; k++)
            sh.name[k] = s[i * 40 + k];
        sh.vsize       = rd32le(s + i * 40 + 8);
        sh.rva         = rd32le(s + i * 40 + 12);
        sh.raw_size    = rd32le(s + i * 40 + 16);
        sh.raw_offset  = rd32le(s + i * 40 + 20);
        sh.characteristics = rd32le(s + i * 40 + 36);
        const uint32_t bytes=sh.vsize>sh.raw_size ?
                              sh.vsize:sh.raw_size;
        if(sh.raw_size &&
           (sh.raw_offset>size || sh.raw_size>size-sh.raw_offset))
            return -5;
        if(sh.rva>img->oh.size_of_image ||
           bytes>img->oh.size_of_image-sh.rva)
            return -5;
        const uint32_t span=sh.rva+bytes;
        if(span>img->image_end)
            img->image_end=span;
    }
    if(img->oh.magic!=OPT_MAGIC_PE32 ||
       img->oh.image_base==0u ||
       img->oh.section_align==0u ||
       img->oh.size_of_image==0u ||
       img->oh.size_of_headers>size ||
       img->oh.size_of_headers>img->oh.size_of_image ||
       img->oh.entry_rva>=img->oh.size_of_image)
        return -5;
    return 0;
}

// --- export/import directory readers (loaded-image view) --------------------
//
// These operate on a *loaded* image (section data already in place). The
// PE directory tables stay little-endian in memory; only IAT slots are
// patched big-endian (payload-visible words).

struct ExportDir {
    uint32_t ordinal_base;
    uint32_t nfuncs;
    uint32_t nnames;
    uint32_t addrfuncs_rva;
    uint32_t addrnames_rva;
    uint32_t addrords_rva;
};

inline bool read_export_dir(const Image &img, const uint8_t *image,
                            uint32_t image_size, ExportDir *out) {
    if (img.oh.magic != OPT_MAGIC_PE32 || img.oh.export_rva == 0 ||
        img.oh.export_size < 40 || img.oh.export_rva > image_size ||
        40u > image_size - img.oh.export_rva ||
        img.oh.export_size > image_size - img.oh.export_rva)
        return false;
    const uint8_t *e = image + img.oh.export_rva;
    out->ordinal_base    = rd32le(e + 16);
    out->nfuncs          = rd32le(e + 20);
    out->nnames          = rd32le(e + 24);
    out->addrfuncs_rva   = rd32le(e + 28);
    out->addrnames_rva   = rd32le(e + 32);
    out->addrords_rva    = rd32le(e + 36);
    if (out->nfuncs == 0u || out->nfuncs > image_size/4u ||
        out->nnames > image_size/2u ||
        out->addrfuncs_rva > image_size ||
        out->addrnames_rva > image_size ||
        out->addrords_rva > image_size ||
        out->nfuncs * 4u > image_size - out->addrfuncs_rva ||
        out->nnames * 4u > image_size - out->addrnames_rva ||
        out->nnames * 2u > image_size - out->addrords_rva)
        return false;
    return out->nfuncs != 0;
}

// Look up `name` in the export directory; returns the function RVA.
inline bool export_lookup(const Image &img, const uint8_t *image,
                          uint32_t image_size, const char *name,
                          uint32_t *out_rva) {
    ExportDir ed;
    if (!read_export_dir(img, image, image_size, &ed))
        return false;
    for (uint32_t i = 0; i < ed.nnames; i++) {
        const uint32_t name_rva = rd32le(image + ed.addrnames_rva + i * 4);
        if (name_rva == 0 || name_rva >= image_size)
            continue;
        uint32_t n=0;
        const uint8_t *t=reinterpret_cast<const uint8_t *>(name);
        for (; name_rva+n<image_size && n<128u; ++n) {
            char x=static_cast<char>(image[name_rva+n]);
            char y=static_cast<char>(t[n]);
            if (x>='A' && x<='Z') x=static_cast<char>(x+32);
            if (y>='A' && y<='Z') y=static_cast<char>(y+32);
            if (x!=y || !x) break;
        }
        if (name_rva+n>=image_size || n>=128u ||
            image[name_rva+n] || t[n])
            continue;
        const uint32_t ord = rd16le(image + ed.addrords_rva + i * 2);
        if (ord >= ed.nfuncs)
            continue;
        const uint32_t func_rva = rd32le(image + ed.addrfuncs_rva + ord * 4);
        // A forwarder RVA points back into the export directory itself
        // (RVA 0 is a legitimate export -- e.g. the first function).
        if (func_rva >= img.oh.export_rva &&
            func_rva < img.oh.export_rva + img.oh.export_size)
            continue; // forwarded export -- not supported
        *out_rva = func_rva;
        return true;
    }
    return false;
}

// Export lookup without discarding forwarder RVAs. A forwarder's RVA
// points into the export directory and contains "OTHERDLL.Func" or
// "OTHERDLL.#123"; WinCE resolves this recursively through its loader.
inline bool export_address(const Image &img,const uint8_t *memory,
                           uint32_t length,const char *symbol,
                           uint32_t ordinal,bool by_ordinal,
                           uint32_t *rva) {
    if(!rva || (!by_ordinal && !symbol))return false;
    ExportDir ed{};
    if(!read_export_dir(img,memory,length,&ed))return false;
    uint32_t index=0u;
    if(by_ordinal) {
        if(ordinal<ed.ordinal_base ||
           ordinal-ed.ordinal_base>=ed.nfuncs)return false;
        index=ordinal-ed.ordinal_base;
    } else {
        bool found=false;
        for(uint32_t n=0;n<ed.nnames;++n) {
            const uint32_t name_rva=rd32le(
                memory+ed.addrnames_rva+n*4u);
            if(!name_rva || name_rva>=length)continue;
            uint32_t k=0u;
            for(;k<128u && k<length-name_rva;++k) {
                unsigned char left=memory[name_rva+k];
                unsigned char right=static_cast<unsigned char>(symbol[k]);
                if(left!=right || !left)break;
            }
            if(k>=128u || k>=length-name_rva ||
               memory[name_rva+k] || symbol[k])continue;
            const uint32_t candidate=rd16le(
                memory+ed.addrords_rva+n*2u);
            if(candidate>=ed.nfuncs)return false;
            index=candidate;
            found=true;break;
        }
        if(!found)return false;
    }
    const uint32_t result=rd32le(
        memory+ed.addrfuncs_rva+index*4u);
    if(!result || result>=length)return false;
    *rva=result;
    return true;
}

struct ImportDesc {
    uint32_t oft_rva;   // OriginalFirstThunk (hint/name array)
    uint32_t name_rva;  // module name
    uint32_t ft_rva;    // FirstThunk (IAT -- what the loader overwrites)
};

inline bool read_import_desc(const Image &img, const uint8_t *image,
                             uint32_t image_size, uint32_t index,
                             ImportDesc *out) {
    if (img.oh.magic != OPT_MAGIC_PE32 || img.oh.import_rva == 0)
        return false;
    // Import descriptors are 20 bytes, bounded by the PE data directory.
    // Prevent integer wrap or wandering into unrelated mapped sections.
    if (img.oh.import_size<20u ||
        index >= img.oh.import_size/20u ||
        img.oh.import_rva>image_size ||
        index > (image_size-img.oh.import_rva)/20u)
        return false;
    const uint32_t off=img.oh.import_rva+index*20u;
    if (off>image_size || 20u>image_size-off)
        return false;
    const uint8_t *d = image + off;
    out->oft_rva  = rd32le(d + 0);
    out->name_rva = rd32le(d + 12);
    out->ft_rva   = rd32le(d + 16);
    // all-zero descriptor = end of table
    return !(out->oft_rva == 0 && out->name_rva == 0 && out->ft_rva == 0);
}

// Read a NUL-terminated string at an RVA inside the image. Returns nullptr
// when out of bounds. `buf` receives up to bufsize-1 chars.
inline const char *rva_string(const uint8_t *image, uint32_t image_size,
                              uint32_t rva, char *buf, uint32_t bufsize) {
    if (!image || !buf || bufsize<2u ||
        rva==0u || rva>=image_size)
        return nullptr;
    uint32_t k=0;
    while (k+1u<bufsize && k<image_size-rva &&
           image[rva+k]) {
        buf[k]=static_cast<char>(image[rva+k]);
        ++k;
    }
    // A truncated or non-terminated WinCE import/export name is invalid.
    if (k>=image_size-rva || image[rva+k]) return nullptr;
    buf[k]='\0';
    return buf;
}

} // namespace pe
