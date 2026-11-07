// NTOS -- "NT loader" for SH4 big-endian COFF EXEs on Hollyhock-3.
//
// An HH3 SDK app that scans \fls0\*.EXE / *.DLL (standard PE images, machine
// 0x01A8), lists them, loads the selected one into the HH3 heap, applies
// relocations (PE .reloc base-relocs for MSVC-linked images, COFF
// DIR32/REL32 object relocs for shasm-linked ones), flushes caches, and
// jumps into the entry point -- the same job WinCE's loader does for
// MIPS/SH PE images, done here for our own BE COFF flavor.
//
// Entry contract for payloads (r4-r7, the .sh4al scalar ABI):
//   int entry(const NtosApi *api /*r4*/, char **argv /*r5*/, char **envp /*r6*/)
// See struct NtosApi below for the syscall table layout.

#include <appdef.h>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <sdk/calc/calc.h>
#include <sdk/os/debug.h>
#include <sdk/os/file.h>
#include <sdk/os/lcd.h>
#include <sdk/os/mem.h>

#include "pe.hpp"
#include "mmu.hpp"

APP_NAME("NTOS")
APP_DESCRIPTION("Big-endian SH4 COFF EXE loader")
APP_AUTHOR("recreate project")
APP_VERSION("0.2")

// ---------------------------------------------------------------------------
// Syscall table handed to payloads (field order == table order)
// ---------------------------------------------------------------------------
using puts_fn = void (*)(const char *s);
using printf_fn = void (*)(unsigned int, unsigned int, bool, int, const char *,
                           ...);
using refresh_fn = void (*)();
using malloc_fn = void *(*)(size_t);
using free_fn = void (*)(void *);
using getkey_fn = uint32_t (*)();

struct NtosApi {
    puts_fn puts;          // [0] console line output
    printf_fn printf_;     // [1] Debug_Printf passthrough
    refresh_fn refresh;    // [2] LCD_Refresh
    malloc_fn malloc_;     // [3] Mem_Malloc (12 MiB heap @ 0x8C280000)
    free_fn free_;         // [4] Mem_Free
    getkey_fn get_key;     // [5] blocking key read, packed
};

static int cur_row = 0;

// Console output goes through Debug_Printf -- the small font (the big
// Debug_PrintString glyphs only fit ~28 columns and drown the screen).
static void ntos_puts(const char *s) {
    if (cur_row > 22) {
        cur_row = 0;
        LCD_Refresh();
    }
    Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0, "%s",
                 s);
    LCD_Refresh();
}

static uint32_t ntos_getkey(void) {
    uint32_t key1 = 0;
    uint32_t key2 = 0;
    getKey(&key1, &key2);
    return (key1 << 8) | (key2 & 0xFFu);
}

static NtosApi g_ntos_api = {
    &ntos_puts,
    Debug_Printf, // SDK declares these as function pointers, not functions
    LCD_Refresh,
    &Mem_Malloc,  // this one really is an inline function in the SDK
    Mem_Free,
    &ntos_getkey,
};

// ---------------------------------------------------------------------------
// Cache maintenance (the same dance the HH3 launcher performs after PT_LOAD)
// ---------------------------------------------------------------------------
static void flush_image(void *addr, uint32_t size) {
    auto *p = static_cast<char *>(addr);
    for (uint32_t i = 0; i < size; i += 32)
        __asm__ volatile("ocbwb @%0" : : "r"(p + i));
    __asm__ volatile("ocbwb @%0" : : "r"(p + size - 1));
    for (uint32_t i = 0; i < size; i += 32)
        __asm__ volatile("icbi @%0" : : "r"(p + i));
    __asm__ volatile("icbi @%0" : : "r"(p + size - 1));
    __asm__ volatile("synco"); // SH4A: synchronize after operand-cache ops
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
struct LoadedSection {
    uint32_t rva;
    uint32_t va; // actual address after placement
    uint32_t raw_size;
    uint32_t vsize;
    uint32_t flags;
};

static int load_coff(const uint8_t *file, uint32_t file_size,
                     uint8_t **out_image, pe::Image *out_hdr,
                     LoadedSection *out_sections, const char **err) {
    *err = "unknown";
    if (pe::parse(file, file_size, out_hdr) != 0) {
        *err = "bad PE header (not SH4BE 0x01A8?)";
        return -1;
    }

    const bool has_base_relocs = out_hdr->oh.basereloc_rva != 0 &&
                                 out_hdr->oh.basereloc_size != 0;
    if (!has_base_relocs) {
        *err = "image has no .reloc directory (mapped mode can run it at "
               "its preferred base; keep relocations for copy-load)";
        return -3;
    }

    uint32_t vsize = out_hdr->oh.size_of_image;
    if (vsize == 0 || vsize > 12u * 1024u * 1024u) {
        vsize = (out_hdr->image_end + 0x1Fu) & ~0x1Fu;
    }
    if (vsize == 0) {
        *err = "empty image";
        return -3;
    }

    // Copy-load: place the image wherever the heap gives it to us, then
    // relocate. The .reloc table itself is little-endian (PE spec); the
    // fixup sites are payload-visible words, so big-endian in memory.
    auto *image = static_cast<uint8_t *>(Mem_Malloc(vsize));
    if (image == nullptr) {
        *err = "out of memory";
        return -2;
    }
    Mem_Memset(image, 0, vsize);

    for (uint32_t i = 0; i < out_hdr->section_count; i++) {
        const pe::SectionHeader &sh = out_hdr->sections[i];
        LoadedSection &ls = out_sections[i];
        ls.rva = sh.rva;
        ls.va = reinterpret_cast<uint32_t>(image) + sh.rva;
        ls.raw_size = sh.raw_size;
        ls.vsize = sh.vsize != 0 ? sh.vsize : sh.raw_size;
        ls.flags = sh.characteristics;

        if ((sh.characteristics & pe::SCN_CNT_UNINITIALIZED_DATA) ||
            sh.raw_size == 0)
            continue; // already zeroed
        if (sh.raw_offset > file_size ||
            sh.raw_size > file_size - sh.raw_offset) {
            *err = "section data past end of file";
            Mem_Free(image);
            return -4;
        }
        Mem_Memcpy(image + sh.rva, file + sh.raw_offset, sh.raw_size);
    }

    // PE base relocations: add delta = actual_base - preferred_base to
    // every HIGHLOW site.
    const uint32_t delta =
        reinterpret_cast<uint32_t>(image) - out_hdr->oh.image_base;
    uint32_t off = out_hdr->oh.basereloc_rva;
    const uint32_t end = off + out_hdr->oh.basereloc_size;
    while (off + 8 <= end && off + 8 <= vsize) {
        const uint32_t page_rva   = pe::rd32le(image + off);
        const uint32_t block_size = pe::rd32le(image + off + 4);
        if (block_size < 8 || off + block_size > end ||
            off + block_size > vsize)
            break;
        const uint32_t n_entries = (block_size - 8) / 2;
        for (uint32_t k = 0; k < n_entries; k++) {
            const uint16_t e = pe::rd16le(image + off + 8 + k * 2);
            const uint8_t type = static_cast<uint8_t>(e >> 12);
            const uint16_t ofs = static_cast<uint16_t>(e & 0x0FFFu);
            if (type == pe::BRELOC_ABSOLUTE)
                continue;
            if (type == pe::BRELOC_HIGHLOW) {
                uint8_t *at = image + page_rva + ofs;
                if (at + 4 > image + vsize)
                    continue;
                pe::wr32be(at, pe::rd32be(at) + delta);
            }
            // other types ignored
        }
        off += block_size;
    }

    flush_image(image, vsize);

    *out_image = image;
    *err = "ok";
    return 0;
}

// ---------------------------------------------------------------------------
// UTLB-mapped loading (WinCE-style modules at their preferred bases)
//
// Same job WinCE's nk.exe does for XIP/RAM modules, adapted to the CP400:
// the MMU is off in the HH3 context (MMUCR.AT = bit 0 = 0), so NTOS takes
// the P0 address space over for the duration of one payload run:
//
//   * coredll (COREDLL.DLL) is copied into the heap and its 4 KiB pages are
//     UTLB-mapped at its preferred base (WinCE maps coredll once and every
//     EXE sees it at the same VA);
//   * the payload EXE is copied and mapped at ITS preferred base, so no
//     relocations are applied at all;
//   * the 0x08100000..0x08103FFF API window is rebuilt exactly like OS330's
//     MMU_ConfigureAddinApiMappings (slots 0x37-0x3A, first word = pointer
//     to the syscall table), plus the VA0 null guard in slot 0x3F;
//   * the EXE's PE import directory is snapped against coredll's export
//     directory (IAT filled with absolute P0 VAs);
//   * on return the MMU is disabled, the UTLB flushed and everything freed,
//     so the calculator returns to the exact pre-load system state.
//
// UTLB slot layout follows OS330: image pages occupy the rotating range
// 0x00..0x36, the API window 0x37..0x3A, the null guard 0x3F. With 4 KiB
// pages that caps coredll+EXE at 0x36 pages (~216 KiB) for now.
// ---------------------------------------------------------------------------
struct MappedImage {
    uint8_t *raw;    // unaligned allocation (for Mem_Free)
    uint8_t *image;  // 4 KiB-aligned copy
    uint32_t vsize;
    uint32_t base;   // preferred virtual base
    uint32_t npages;
};

static void *align_page(void *p) {
    return reinterpret_cast<void *>(
        (reinterpret_cast<uint32_t>(p) + 0xFFFu) & ~0xFFFu);
}

// Copy an image into 4 KiB-aligned heap memory without applying any
// relocations -- it will execute at its preferred base via the UTLB.
static int load_mapped(const uint8_t *file, uint32_t file_size,
                       const pe::Image &hdr, MappedImage *out_mi,
                       const char **err) {
    *err = "unknown";
    if (hdr.oh.magic != pe::OPT_MAGIC_PE32 || hdr.oh.image_base == 0 ||
        hdr.oh.image_base < 0x00100000u ||
        hdr.oh.image_base > 0x07FF0000u) {
        *err = "no valid preferred base (needs PE32 optional header)";
        return -1;
    }
    uint32_t vsize = hdr.oh.size_of_image;
    if (vsize == 0 || vsize > 8u * 1024u * 1024u)
        vsize = (hdr.image_end + 0xFFFu) & ~0xFFFu;
    if (vsize == 0) {
        *err = "empty image";
        return -1;
    }
    // Keep clear of the reserved P0 windows: the API window and page 0.
    if (hdr.oh.image_base + vsize > mmu::VA_API_WINDOW &&
        hdr.oh.image_base < mmu::VA_API_WINDOW + 0x4000u) {
        *err = "preferred base collides with the 0x08100000 API window";
        return -1;
    }

    auto *raw = static_cast<uint8_t *>(Mem_Malloc(vsize + 0xFFFu));
    if (raw == nullptr) {
        *err = "out of memory";
        return -2;
    }
    auto *image = static_cast<uint8_t *>(align_page(raw));
    Mem_Memset(image, 0, vsize);

    for (uint32_t i = 0; i < hdr.section_count; i++) {
        const pe::SectionHeader &sh = hdr.sections[i];
        if ((sh.characteristics & pe::SCN_CNT_UNINITIALIZED_DATA) ||
            sh.raw_size == 0)
            continue;
        if (sh.raw_offset > file_size ||
            sh.raw_size > file_size - sh.raw_offset ||
            sh.rva + sh.raw_size > vsize) {
            *err = "section data out of bounds";
            Mem_Free(raw);
            return -3;
        }
        Mem_Memcpy(image + sh.rva, file + sh.raw_offset, sh.raw_size);
    }

    out_mi->raw = raw;
    out_mi->image = image;
    out_mi->vsize = vsize;
    out_mi->base = hdr.oh.image_base;
    out_mi->npages = (vsize + 0xFFFu) >> 12;
    *err = "ok";
    return 0;
}

// Snap the EXE's PE import directory against coredll's export directory
// (the WinCE IAT snap): each FirstThunk slot receives the absolute P0 VA
// of the resolved export -- written big-endian, since the payload reads
// the slots with plain SH4 BE word loads. The directory tables themselves
// stay little-endian in memory. Plain P1 memory operations -- the MMU is
// still off here. Returns the number of snapped imports, negative on error.
static int snap_imports(const pe::Image &exe_hdr, const MappedImage *exe,
                        const pe::Image *dll_hdr, const MappedImage *dll,
                        char *tmp, uint32_t tmp_size, const char **err) {
    *err = "unknown";
    pe::ExportDir ed{};
    bool have_ed = false;
    int total = 0;

    for (uint32_t di = 0;; di++) {
        pe::ImportDesc d{};
        if (!pe::read_import_desc(exe_hdr, exe->image, exe->vsize, di, &d))
            break; // end of import table (or none at all)
        if (dll->image == nullptr) {
            *err = "image has an import table but COREDLL.DLL is missing";
            return -2;
        }
        if (!have_ed) {
            if (!pe::read_export_dir(*dll_hdr, dll->image, dll->vsize,
                                     &ed)) {
                *err = "COREDLL.DLL has no usable export directory";
                return -3;
            }
            have_ed = true;
        }
        if (d.oft_rva == 0 || d.ft_rva == 0 || d.name_rva == 0 ||
            d.oft_rva + 4 > exe->vsize || d.ft_rva + 4 > exe->vsize) {
            *err = "malformed import descriptor";
            return -1;
        }

        for (uint32_t i = 0;; i++) {
            const uint32_t hn_off = d.oft_rva + i * 4;
            const uint32_t ft_off = d.ft_rva + i * 4;
            if (hn_off + 4 > exe->vsize || ft_off + 4 > exe->vsize) {
                *err = "import thunk arrays out of bounds";
                return -4;
            }
            const uint32_t entry = pe::rd32le(exe->image + hn_off);
            if (entry == 0)
                break;

            uint32_t func_rva = 0;
            if (entry & 0x80000000u) {
                const uint32_t ord = entry & 0xFFFFu;
                if (ord < ed.ordinal_base ||
                    ord - ed.ordinal_base >= ed.nfuncs) {
                    *err = "ordinal import out of range";
                    return -5;
                }
                func_rva = pe::rd32le(
                    dll->image + ed.addrfuncs_rva +
                    (ord - ed.ordinal_base) * 4);
            } else {
                if (entry + 2 >= exe->vsize ||
                    pe::rva_string(exe->image, exe->vsize, entry + 2, tmp,
                                   tmp_size) == nullptr) {
                    *err = "bad import-by-name record";
                    return -6;
                }
                if (!pe::export_lookup(*dll_hdr, dll->image, dll->vsize,
                                       tmp, &func_rva)) {
                    *err = "import not found in COREDLL";
                    return -7;
                }
            }
            if (func_rva >= dll_hdr->oh.export_rva &&
                func_rva <
                    dll_hdr->oh.export_rva + dll_hdr->oh.export_size) {
                *err = "resolved import is a forwarder (unsupported)";
                return -8;
            }
            pe::wr32be(exe->image + ft_off, dll->base + func_rva);
            total++;
        }
    }
    *err = "ok";
    return total;
}

// Full UTLB-mapped run. Returns 0 on success (payload return value in
// *out_ret) or -1 on loader error with `err` set. On every exit path the
// MMU is disabled, the UTLB flushed and all allocations released.
static int run_mapped(uint8_t *file, uint32_t fsize, const pe::Image &hdr,
                      const char *core_path, uint32_t core_size,
                      int *out_ret, const char **err) {
    *err = "unknown";
    MappedImage exe{};
    MappedImage dll{};
    pe::Image dll_hdr{};
    uint8_t *dll_file = nullptr;
    uint8_t *api_raw = nullptr;
    uint8_t *api_block = nullptr;
    uint32_t slot = 0;
    int rc = -1;
    // Set+way bookkeeping for the UTLB install (see below): pages whose VA
    // bits 16:12 land in the same of the 32 UTLB sets must occupy different
    // ways -- the set is derived from the VPN, the slot only matters on
    // fully-associative hardware.
    uint8_t set_ways[32] = {};
    auto install_page = [&](uint32_t va, uint32_t pa, uint32_t page_slot) {
        const uint32_t set = (va >> 12) & 0x1Fu;
        if (set_ways[set] > 3)
            return false;
        mmu::map_page_at_index(va, pa, page_slot, set_ways[set]++);
        return true;
    };

    // --- coredll copy ---------------------------------------------------
    if (core_path != nullptr) {
        int fd = File_Open(core_path, FILE_OPEN_READ);
        if (fd < 0) {
            *err = "File_Open(COREDLL.DLL) failed";
            goto done;
        }
        dll_file = static_cast<uint8_t *>(Mem_Malloc(core_size));
        if (dll_file == nullptr) {
            File_Close(fd);
            *err = "OOM reading COREDLL.DLL";
            goto done;
        }
        if (File_Read(fd, dll_file, static_cast<int>(core_size)) !=
            static_cast<int>(core_size)) {
            File_Close(fd);
            *err = "File_Read(COREDLL.DLL) failed";
            goto done;
        }
        File_Close(fd);
        if (pe::parse(dll_file, core_size, &dll_hdr) != 0) {
            *err = "COREDLL.DLL: bad PE header";
            goto done;
        }
        if (load_mapped(dll_file, core_size, dll_hdr, &dll, err) != 0)
            goto done;
    }

    // --- EXE copy ---------------------------------------------------------
    if (load_mapped(file, fsize, hdr, &exe, err) != 0)
        goto done;

    // --- API window block (OS330 MMU_ConfigureAddinApiMappings recipe) ----
    api_raw = static_cast<uint8_t *>(Mem_Malloc(0x4000u + 0xFFFu));
    if (api_raw == nullptr) {
        *err = "OOM for the API window block";
        goto done;
    }
    api_block = static_cast<uint8_t *>(align_page(api_raw));
    Mem_Memset(api_block, 0, 0x4000);
    *reinterpret_cast<void **>(api_block) = &g_ntos_api;

    // --- UTLB slot budget ---------------------------------------------------
    if (exe.npages + dll.npages + mmu::API_WINDOW_PAGES + 1 >
        mmu::SLOT_ROTATING_LAST + 1) {
        *err = "images too large for the UTLB slot budget (4 KiB pages)";
        goto done;
    }

    // --- import snapping (plain P1 writes, MMU still off) ------------------
    if (hdr.oh.import_rva != 0) {
        char modname[24];
        const char *serr = nullptr;
        const int snapped =
            snap_imports(hdr, &exe, &dll_hdr, &dll, modname,
                         sizeof(modname), &serr);
        if (snapped < 0) {
            *err = serr;
            goto done;
        }
        Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0,
                     "Snapped %d import(s) from COREDLL", snapped);
        LCD_Refresh();
    }

    // --- write back the copies through P1 ----------------------------------
    flush_image(exe.image, exe.vsize);
    if (dll.image != nullptr)
        flush_image(dll.image, dll.vsize);
    flush_image(api_block, 0x4000);

    // --- install UTLB entries (AT is still off) ------------------------------
    mmu::disable_at_flush(); // flush the UTLB; AT guaranteed off here
    for (uint32_t p = 0; p < dll.npages; p++, slot++)
        if (!install_page(dll.base + p * 0x1000u,
                          mmu::phys_addr(dll.image + p * 0x1000u), slot)) {
            *err = "too many pages in one UTLB set";
            goto done;
        }
    for (uint32_t p = 0; p < exe.npages; p++, slot++)
        if (!install_page(exe.base + p * 0x1000u,
                          mmu::phys_addr(exe.image + p * 0x1000u), slot)) {
            *err = "too many pages in one UTLB set";
            goto done;
        }
    for (uint32_t i = 0; i < mmu::API_WINDOW_PAGES; i++)
        if (!install_page(mmu::VA_API_WINDOW + i * 0x1000u,
                          mmu::phys_addr(api_block + i * 0x1000u),
                          mmu::SLOT_API_FIRST + i)) {
            *err = "too many pages in one UTLB set";
            goto done;
        }
    if (!install_page(0, 0, mmu::SLOT_NULL_GUARD)) {
        *err = "too many pages in one UTLB set";
        goto done;
    }

    // --- enable translation, drop stale P0-indexed cache lines --------------
    mmu::enable_at();
    mmu::cache_flush_invalidate_block(reinterpret_cast<void *>(exe.base),
                                      exe.vsize);
    if (dll.image != nullptr)
        mmu::cache_flush_invalidate_block(reinterpret_cast<void *>(dll.base),
                                          dll.vsize);
    mmu::cache_flush_invalidate_block(
        reinterpret_cast<void *>(mmu::VA_API_WINDOW), 0x4000);

    // --- run ------------------------------------------------------------------
    {
        using entry_fn = int (*)(const NtosApi *, char **, char **);
        entry_fn entry =
            reinterpret_cast<entry_fn>(exe.base + hdr.oh.entry_rva);
        rc = entry(&g_ntos_api, nullptr, nullptr);
    }
    *out_ret = rc;
    *err = "ok";
    rc = 0;

done:
    // Restore the system exactly as we found it: AT off + UTLB flushed.
    mmu::disable_at_flush();
    if (api_raw != nullptr)
        Mem_Free(api_raw);
    if (exe.raw != nullptr)
        Mem_Free(exe.raw);
    if (dll.raw != nullptr)
        Mem_Free(dll.raw);
    if (dll_file != nullptr)
        Mem_Free(dll_file);
    return rc;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
constexpr int kMaxFiles = 12;
constexpr int kMaxName = 25;

static char_const16_t g_find_pattern_exe[] = {
    '\\', 'f', 'l', 's', '0', '\\', '*', '.', 'E', 'X', 'E', 0,
};
static char_const16_t g_find_pattern_dll[] = {
    '\\', 'f', 'l', 's', '0', '\\', '*', '.', 'D', 'L', 'L', 0,
};

static void banner(void) {
    fillScreen(color(0, 0, 40));
    Debug_Printf(0, 0, true, 0, "%s", "NTOS 0.2 - SH4BE PE loader + UTLB");
    Debug_Printf(0, 1, false, 0, "%s", "Select an .EXE (SH4BE PE)");
    LCD_Refresh();
    cur_row = 3;
}

static void tiny_utoa(uint32_t v, char *buf) {
    char tmp[12];
    int n = 0;
    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (v != 0) {
        tmp[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    for (int k = 0; k < n; k++)
        buf[k] = tmp[n - 1 - k];
    buf[n] = '\0';
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    while (true) {
    banner();

    // Scan flash for payload images (*.EXE) and modules (*.DLL). NTOS maps
    // a COREDLL.DLL / COREDLL.EXE as the shared module every EXE imports
    // from; it is not shown in the picker. File_FindFirst returns UCS-16
    // names; File_Open takes ANSI paths, so convert on the way in.
    char names[kMaxFiles][kMaxName + 1] = {};
    uint32_t sizes[kMaxFiles] = {};
    int count = 0;

    for (const char_const16_t *pattern :
         {g_find_pattern_exe, g_find_pattern_dll}) {
        int find = 0;
        char_const16_t name[32] = {};
        File_FindInfo info{};
        if (File_FindFirst(pattern, &find, name, &info) == 0) {
            do {
                if (info.type == File_FindInfo::EntryTypeFile &&
                    count < kMaxFiles) {
                    int k = 0;
                    for (; k < kMaxName && name[k] != 0; k++)
                        names[count][k] = static_cast<char>(name[k]);
                    names[count][k] = '\0';
                    sizes[count] = info.size;
                    count++;
                }
                if (File_FindNext(find, name, &info) != 0)
                    break;
            } while (true);
            File_FindClose(find);
        }
    }

    // Build the pickable list (everything except COREDLL.*) and find the
    // coredll module for the loader.
    int pick[kMaxFiles] = {};
    int coredll_idx = -1;
    {
        int npick = 0;
        for (int i = 0; i < count; i++) {
            const char stem[] = "COREDLL";
            int k = 0;
            while (stem[k] != '\0' && names[i][k] != '\0' &&
                   (names[i][k] | 0x20) == (stem[k] | 0x20))
                k++;
            if (stem[k] != '\0' || names[i][k] != '.')
                pick[npick++] = i;
            else
                coredll_idx = i;
        }
        count = npick;
    }

    if (count == 0) {
        Debug_SetCursorPosition(0, 3);
        Debug_PrintString("No .EXE files found on \\fls0.", false);
        Debug_SetCursorPosition(0, 4);
        Debug_PrintString("Put an SH4BE PE EXE there and retry.", false);
        LCD_Refresh();
        Debug_WaitKey();
        return 0;
    }

    for (int i = 0; i < count; i++) {
        char line[48];
        char sz[12];
        tiny_utoa(sizes[pick[i]], sz);
        int p = 0;
        line[p++] = static_cast<char>('0' + i);
        line[p++] = ':';
        line[p++] = ' ';
        for (int k = 0; k < kMaxName && names[pick[i]][k] != '\0'; k++)
            line[p++] = names[pick[i]][k];
        line[p++] = ' ';
        for (int k = 0; sz[k] != '\0' && p < 46; k++)
            line[p++] = sz[k];
        line[p] = '\0';
        Debug_SetCursorPosition(2, static_cast<unsigned int>(3 + i));
        Debug_PrintString(line, false);
    }
    LCD_Refresh();

    // Selection: EXE runs, CLEAR (power) quits, ADD/UP/DOWN cycles.
    int selected = 0;
    bool quit = false;
    while (!quit) {
        uint32_t key1 = 0;
        uint32_t key2 = 0;
        getKey(&key1, &key2);
        if ((key1 | key2) != 0) {
            if (testKey(key1, key2, KEY_EXE)) {
                quit = true;
            } else if (testKey(key1, key2, KEY_CLEAR)) {
                return 0; // exits NTOS back to the launcher
            } else if (testKey(key1, key2, KEY_ADD) ||
                       testKey(0, key2, KEY_UP) ||
                       testKey(0, key2, KEY_DOWN)) {
                selected = (selected + 1) % count;
                uint32_t d1 = 0;
                uint32_t d2 = 0;
                do {
                    getKey(&d1, &d2);
                } while ((d1 | d2) != 0);
                Debug_Printf(0, 2, true, 0, "Selected: %d of %d      ",
                             selected + 1, count);
                LCD_Refresh();
            }
        }
    }

    // Read the selected file fully. File_Open takes an ANSI path.
    char path[8 + kMaxName + 1];
    {
        const char prefix[] = "\\fls0\\";
        int p = 0;
        for (; prefix[p] != '\0'; p++)
            path[p] = prefix[p];
        const int sel = pick[selected];
        for (int k = 0; k < kMaxName && names[sel][k] != '\0'; k++)
            path[p++] = names[sel][k];
        path[p] = '\0';
    }

    int fd = File_Open(path, FILE_OPEN_READ);
    if (fd < 0) {
        ntos_puts("File_Open failed");
        Debug_WaitKey();
        return 1;
    }
    uint32_t fsize = sizes[pick[selected]];
    auto *file = static_cast<uint8_t *>(Mem_Malloc(fsize != 0 ? fsize : 1));
    if (file == nullptr) {
        ntos_puts("OOM reading file");
        File_Close(fd);
        Debug_WaitKey();
        return 1;
    }
    if (File_Read(fd, file, static_cast<int>(fsize)) !=
        static_cast<int>(fsize)) {
        ntos_puts("File_Read failed");
        Mem_Free(file);
        File_Close(fd);
        Debug_WaitKey();
        return 1;
    }
    File_Close(fd);

    // Parse the header once to pick the load path.
    pe::Image hdr{};
    if (pe::parse(file, fsize, &hdr) != 0) {
        ntos_puts("Bad PE header (not SH4BE 0x01A8?)");
        Mem_Free(file);
        Debug_WaitKey();
        return 2;
    }

    int ret;
    if (!mmu::at_enabled() && hdr.oh.magic == pe::OPT_MAGIC_PE32 &&
        hdr.oh.image_base != 0) {
        // UTLB-mapped mode: copy both images into the heap and map their
        // pages at the preferred bases; snap imports against COREDLL.
        const char *core_path = nullptr;
        uint32_t core_size = 0;
        char core_path_buf[8 + kMaxName + 1];
        if (coredll_idx >= 0) {
            const char prefix[] = "\\fls0\\";
            int p = 0;
            for (; prefix[p] != '\0'; p++)
                core_path_buf[p] = prefix[p];
            for (int j = 0; names[coredll_idx][j] != '\0'; j++)
                core_path_buf[p++] = names[coredll_idx][j];
            core_path_buf[p] = '\0';
            core_path = core_path_buf;
            core_size = sizes[coredll_idx];
        }
        ntos_puts("Mode: UTLB-mapped (preferred base)");
        const char *err = nullptr;
        if (run_mapped(file, fsize, hdr, core_path, core_size, &ret,
                       &err) != 0) {
            Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0,
                         "Mapped load failed: %s", err);
            LCD_Refresh();
            Mem_Free(file);
            Debug_WaitKey();
            return 2;
        }
    } else {
        if (mmu::at_enabled())
            ntos_puts("MMU already on: copy-load fallback");
        // Legacy copy-load + relocate, entry from wherever the heap put us.
        LoadedSection sections[8] = {};
        uint8_t *image = nullptr;
        const char *err = nullptr;
        if (load_coff(file, fsize, &image, &hdr, sections, &err) != 0) {
            Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0,
                         "Load failed: %s", err);
            LCD_Refresh();
            Mem_Free(file);
            Debug_WaitKey();
            return 2;
        }

        Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0,
                     "Loaded at 0x%08lX entry 0x%08lX",
                     static_cast<unsigned long>(
                         reinterpret_cast<uintptr_t>(image)),
                     static_cast<unsigned long>(
                         reinterpret_cast<uintptr_t>(image)) +
                         hdr.oh.entry_rva);
        LCD_Refresh();

        // Entry contract: r4 = &NtosApi, r5 = argv, r6 = envp.
        using entry_fn = int (*)(const NtosApi *, char **, char **);
        entry_fn entry = reinterpret_cast<entry_fn>(
            reinterpret_cast<uint32_t>(image) + hdr.oh.entry_rva);
        ret = entry(&g_ntos_api, nullptr, nullptr);
        Mem_Free(image);
    }

    Mem_Free(file);
    Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0,
                  "Payload returned 0x%08lX",
                  static_cast<unsigned long>(static_cast<uint32_t>(ret)));
    Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0, "%s",
                 "EXE: pick again   CLEAR: exit");
    LCD_Refresh();
    Debug_WaitKey();
    } // relaunch loop: back to the banner + picker
}
