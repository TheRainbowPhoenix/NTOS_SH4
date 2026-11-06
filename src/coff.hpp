// coff.hpp -- big-endian COFF EXE structures + byte-exact readers.
//
// NTOS loads SH4 big-endian COFF executables, machine 0x01A8 (SH4 BE),
// produced either by the patched shasm (tools/make_shasm_be.py, MIPS-style
// new machine code) or the MSVC -B2 backend route. All header fields in the
// file are big-endian; the calculator CPU is BE too, but values are still
// assembled from bytes explicitly so the parser stays trivially correct.
//
// Two relocation flavors are supported:
//   * object-style: per-section COFF relocation tables + symbol table
//     (shasm-linked images)
//   * PE-style: .reloc base-relocation directory from the PE32 optional
//     header's data directory #5 (MSVC-linked /FIXED images, e.g. the
//     examples/msvc_hello_sh4al route with preferred base 0x400000)
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace coff {

constexpr uint16_t MACHINE_SH4BE = 0x01A8; // make_shasm_be.py extension
constexpr uint16_t MACHINE_SH4LE = 0x01A6;

// File header flags
constexpr uint16_t F_RELFLG = 0x0001; // relocation info stripped
constexpr uint16_t F_EXEC   = 0x0002; // executable image
constexpr uint16_t F_LNNO   = 0x0004;
constexpr uint16_t F_LSYMS  = 0x0008;
constexpr uint16_t F_AR32WR = 0x0100;

// Section flags (styp)
constexpr uint32_t STYP_TEXT = 0x20;
constexpr uint32_t STYP_DATA = 0x40;
constexpr uint32_t STYP_BSS  = 0x80;

// Relocation types (object-style)
constexpr uint16_t REL_DIR32 = 0x0001;
constexpr uint16_t REL_REL32 = 0x0014;

// PE base relocation types
constexpr uint8_t  BRELOC_ABSOLUTE = 0;
constexpr uint8_t  BRELOC_HIGHLOW  = 3;

// PE32 optional header data directory indices
constexpr int DATADIR_EXPORT     = 0;
constexpr int DATADIR_IMPORT     = 1;
constexpr int DATADIR_BASERELOC  = 5;

struct FileHeader {
    uint16_t machine;
    uint16_t nsections;
    uint32_t timestamp;
    uint32_t symtab_offset;
    uint32_t nsymbols;
    uint16_t opthdr_size;
    uint16_t flags;
};

struct OptHeader {
    uint16_t magic;        // 0x010B (PE32)
    uint8_t  major_link;
    uint8_t  minor_link;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t entry_rva;
    uint32_t text_rva;
    uint32_t data_rva;
    uint32_t image_base;   // preferred load address
    uint32_t section_align;
    uint32_t file_align;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    // data directory #0 (export table), 0/0 if absent
    uint32_t export_rva;
    uint32_t export_size;
    // data directory #1 (import table), 0/0 if absent
    uint32_t import_rva;
    uint32_t import_size;
    // data directory #5 (base relocation table), 0/0 if absent
    uint32_t basereloc_rva;
    uint32_t basereloc_size;
};

struct SectionHeader {
    char     name[8];
    uint32_t vsize;
    uint32_t rva;
    uint32_t raw_size;
    uint32_t raw_offset;
    uint32_t reloc_offset;
    uint32_t lineno_offset;
    uint16_t nrelocs;
    uint16_t nlinenos;
    uint32_t flags;
};

struct RelocEntry {
    uint32_t rva;      // offset within the section (object-style)
    uint16_t symindex;
    uint16_t type;
};

struct Image {
    FileHeader  fh;
    OptHeader   oh;      // valid only if fh.opthdr_size >= 28
    uint32_t    section_count;
    SectionHeader sections[8];
    uint32_t    image_end;  // max(rva + max(vsize, raw_size)) across sections
};

// --- byte-exact big-endian readers ------------------------------------------
inline uint16_t rd16(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
inline uint32_t rd32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

inline void wr32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

// Parse the COFF file header + optional header + section table from `data`.
// Returns 0 on success, negative error code otherwise.
inline int parse(const uint8_t *data, uint32_t size, Image *img) {
    if (size < 24)
        return -1;
    img->fh.machine       = rd16(data + 0);
    img->fh.nsections     = rd16(data + 2);
    img->fh.timestamp     = rd32(data + 4);
    img->fh.symtab_offset = rd32(data + 8);
    img->fh.nsymbols      = rd32(data + 12);
    img->fh.opthdr_size   = rd16(data + 16);
    img->fh.flags         = rd16(data + 18);

    if (img->fh.machine != MACHINE_SH4BE)
        return -2; // not a big-endian SH4 COFF
    if (img->fh.nsections == 0 || img->fh.nsections > 8)
        return -3;
    // The COFF file header is 20 bytes (no PE signature here): the optional
    // header starts at 20 and the section table at 20 + opthdr_size.
    if (size < 20u + img->fh.opthdr_size + img->fh.nsections * 40u)
        return -4;

    if (img->fh.opthdr_size >= 28) {
        const uint8_t *o = data + 20;
        img->oh.magic         = rd16(o + 0);
        img->oh.major_link    = o[2];
        img->oh.minor_link    = o[3];
        img->oh.text_size     = rd32(o + 4);
        img->oh.data_size     = rd32(o + 8);
        img->oh.bss_size      = rd32(o + 12);
        img->oh.entry_rva     = rd32(o + 16);
        img->oh.text_rva      = rd32(o + 20);
        img->oh.data_rva      = rd32(o + 24);
        img->oh.image_base    = rd32(o + 28);
        img->oh.section_align = (img->fh.opthdr_size >= 36) ? rd32(o + 32) : 16;
        img->oh.file_align    = (img->fh.opthdr_size >= 40) ? rd32(o + 36) : 16;
        img->oh.size_of_image = (img->fh.opthdr_size >= 72) ? rd32(o + 68) : 0;
        img->oh.size_of_headers = (img->fh.opthdr_size >= 76) ? rd32(o + 72) : 0;
        // PE32 data directories start at optional-header offset 96.
        // Directory #0 (export table) sits at 96 + 0*8 = 96.
        // Directory #1 (import table) sits at 96 + 1*8 = 104.
        // Directory #5 (base relocation table) sits at 96 + 5*8 = 136.
        if (img->fh.opthdr_size >= 144) {
            img->oh.export_rva     = rd32(o + 96);
            img->oh.export_size    = rd32(o + 100);
            img->oh.import_rva     = rd32(o + 104);
            img->oh.import_size    = rd32(o + 108);
            img->oh.basereloc_rva  = rd32(o + 136);
            img->oh.basereloc_size = rd32(o + 140);
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
        img->oh.size_of_image = 0;
        img->oh.size_of_headers = 0;
        img->oh.export_rva = 0;
        img->oh.export_size = 0;
        img->oh.import_rva = 0;
        img->oh.import_size = 0;
        img->oh.basereloc_rva = 0;
        img->oh.basereloc_size = 0;
    }

    const uint8_t *s = data + 20 + img->fh.opthdr_size;
    img->section_count = img->fh.nsections;
    img->image_end = 0;
    for (uint32_t i = 0; i < img->section_count; i++) {
        SectionHeader &sh = img->sections[i];
        for (int k = 0; k < 8; k++)
            sh.name[k] = s[i * 40 + k];
        sh.vsize        = rd32(s + i * 40 + 8);
        sh.rva          = rd32(s + i * 40 + 12);
        sh.raw_size     = rd32(s + i * 40 + 16);
        sh.raw_offset   = rd32(s + i * 40 + 20);
        sh.reloc_offset = rd32(s + i * 40 + 24);
        sh.lineno_offset= rd32(s + i * 40 + 28);
        sh.nrelocs      = rd16(s + i * 40 + 32);
        sh.nlinenos     = rd16(s + i * 40 + 34);
        sh.flags        = rd32(s + i * 40 + 36);
        uint32_t span = sh.rva + (sh.vsize > sh.raw_size ? sh.vsize : sh.raw_size);
        if (span > img->image_end)
            img->image_end = span;
    }
    return 0;
}

// --- PE export/import directory readers (loaded-image view) -----------------
//
// These operate on a *loaded* image (section data already in place, header
// fields still big-endian in the file's byte order). All RVAs are relative
// to the image base, exactly as the WinCE loader sees them.

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
    if (img.oh.magic != 0x010B || img.oh.export_rva == 0 ||
        img.oh.export_size < 40 || img.oh.export_rva + 40 > image_size)
        return false;
    const uint8_t *e = image + img.oh.export_rva;
    out->ordinal_base    = rd32(e + 16);
    out->nfuncs          = rd32(e + 20);
    out->nnames          = rd32(e + 24);
    out->addrfuncs_rva   = rd32(e + 28);
    out->addrnames_rva   = rd32(e + 32);
    out->addrords_rva    = rd32(e + 36);
    if (out->addrfuncs_rva + out->nfuncs * 4 > image_size ||
        out->addrnames_rva + out->nnames * 4 > image_size ||
        out->addrords_rva + out->nnames * 2 > image_size)
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
        const uint32_t name_rva =
            rd32(image + ed.addrnames_rva + i * 4);
        if (name_rva == 0 || name_rva >= image_size)
            continue;
        const uint8_t *s = image + name_rva;
        const uint8_t *t = reinterpret_cast<const uint8_t *>(name);
        while (*s != 0 && *t != 0 &&
               (*s == *t || (*s | 0x20) == (*t | 0x20))) {
            s++;
            t++;
        }
        if (*s != 0 || *t != 0)
            continue;
        const uint32_t ord = rd16(image + ed.addrords_rva + i * 2);
        if (ord >= ed.nfuncs)
            continue;
        const uint32_t func_rva = rd32(image + ed.addrfuncs_rva + ord * 4);
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

struct ImportDesc {
    uint32_t oft_rva;   // OriginalFirstThunk (hint/name array)
    uint32_t name_rva;  // module name
    uint32_t ft_rva;    // FirstThunk (IAT -- what the loader overwrites)
};

inline bool read_import_desc(const Image &img, const uint8_t *image,
                             uint32_t image_size, uint32_t index,
                             ImportDesc *out) {
    if (img.oh.magic != 0x010B || img.oh.import_rva == 0)
        return false;
    const uint32_t off = img.oh.import_rva + index * 20;
    if (off + 20 > image_size)
        return false;
    const uint8_t *d = image + off;
    out->oft_rva  = rd32(d + 0);
    out->name_rva = rd32(d + 12);
    out->ft_rva   = rd32(d + 16);
    // all-zero descriptor = end of table
    return !(out->oft_rva == 0 && out->name_rva == 0 && out->ft_rva == 0);
}

// Read a NUL-terminated string at an RVA inside the image. Returns nullptr
// when out of bounds. `buf` receives up to bufsize-1 chars.
inline const char *rva_string(const uint8_t *image, uint32_t image_size,
                              uint32_t rva, char *buf, uint32_t bufsize) {
    if (rva == 0 || rva >= image_size)
        return nullptr;
    uint32_t k = 0;
    while (rva + k < image_size && image[rva + k] != 0 && k + 1 < bufsize) {
        buf[k] = static_cast<char>(image[rva + k]);
        k++;
    }
    buf[k] = '\0';
    return buf;
}

} // namespace coff
