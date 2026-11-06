// NTOS -- "NT loader" for SH4 big-endian COFF EXEs on Hollyhock-3.
//
// An HH3 SDK app that scans \fls0\*.NTX (big-endian COFF EXEs, machine
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
#include <sdk/calc/calc.h>
#include <sdk/os/debug.h>
#include <sdk/os/file.h>
#include <sdk/os/lcd.h>
#include <sdk/os/mem.h>

#include "coff.hpp"

APP_NAME("NTOS")
APP_DESCRIPTION("Big-endian SH4 COFF EXE loader")
APP_AUTHOR("recreate project")
APP_VERSION("0.1")

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

static void ntos_puts(const char *s) {
    if (cur_row > 28) {
        cur_row = 0;
        LCD_Refresh();
    }
    Debug_SetCursorPosition(0, static_cast<unsigned int>(cur_row++));
    Debug_PrintString(s, false);
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
                     uint8_t **out_image, coff::Image *out_hdr,
                     LoadedSection *out_sections, const char **err) {
    *err = "unknown";
    if (coff::parse(file, file_size, out_hdr) != 0) {
        *err = "bad COFF header (not SH4BE 0x01A8?)";
        return -1;
    }

    // F_RELFLG clear only means "not marked stripped"; it does NOT
    // prove that the file has a usable COFF relocation table.
    bool has_obj_relocs = false;
    if (!(out_hdr->fh.flags & coff::F_RELFLG) &&
        out_hdr->fh.symtab_offset <= file_size &&
        out_hdr->fh.nsymbols != 0 &&
        out_hdr->fh.nsymbols <=
            (file_size - out_hdr->fh.symtab_offset) / 18u) {
        for (uint32_t i = 0; i < out_hdr->section_count; i++) {
            const coff::SectionHeader &s = out_hdr->sections[i];
            if (s.nrelocs != 0 &&
                s.reloc_offset <= file_size &&
                s.nrelocs <= (file_size - s.reloc_offset) / 10u) {
                has_obj_relocs = true;
                break;
            }
        }
    }
    const bool has_base_relocs = out_hdr->oh.basereloc_rva != 0 &&
                                 out_hdr->oh.basereloc_size >= 8;
    if (!has_obj_relocs && !has_base_relocs) {
        *err = "no usable relocations: link with --emit-relocs and pack .reloc";
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
    // relocate. PE base relocs make the image position-independent; COFF
    // object relocs rebuild absolute addresses from the symbol table.
    auto *image = static_cast<uint8_t *>(Mem_Malloc(vsize));
    if (image == nullptr) {
        *err = "out of memory";
        return -2;
    }
    Mem_Memset(image, 0, vsize);

    for (uint32_t i = 0; i < out_hdr->section_count; i++) {
        const coff::SectionHeader &sh = out_hdr->sections[i];
        LoadedSection &ls = out_sections[i];
        ls.rva = sh.rva;
        ls.va = reinterpret_cast<uint32_t>(image) + sh.rva;
        ls.raw_size = sh.raw_size;
        ls.vsize = sh.vsize != 0 ? sh.vsize : sh.raw_size;
        ls.flags = sh.flags;

        if ((sh.flags & coff::STYP_BSS) || sh.raw_size == 0)
            continue; // already zeroed
        if (sh.raw_offset > file_size ||
            sh.raw_size > file_size - sh.raw_offset) {
            *err = "section data past end of file";
            Mem_Free(image);
            return -4;
        }
        Mem_Memcpy(image + sh.rva, file + sh.raw_offset, sh.raw_size);
    }

    if (has_base_relocs) {
        // PE-style: walk the base relocation blocks and add
        // delta = actual_base - preferred_base to every HIGHLOW entry.
        const uint32_t delta =
            reinterpret_cast<uint32_t>(image) - out_hdr->oh.image_base;
        uint32_t off = out_hdr->oh.basereloc_rva;
        const uint32_t end = off + out_hdr->oh.basereloc_size;
        while (off + 8 <= end && off + 8 <= vsize) {
            const uint32_t page_rva   = coff::rd32(image + off);
            const uint32_t block_size = coff::rd32(image + off + 4);
            if (block_size < 8 || off + block_size > end ||
                off + block_size > vsize)
                break;
            const uint32_t n_entries = (block_size - 8) / 2;
            for (uint32_t k = 0; k < n_entries; k++) {
                const uint16_t e =
                    coff::rd16(image + off + 8 + k * 2);
                const uint8_t type = static_cast<uint8_t>(e >> 12);
                const uint16_t ofs = static_cast<uint16_t>(e & 0x0FFFu);
                if (type == coff::BRELOC_ABSOLUTE)
                    continue;
                if (type == coff::BRELOC_HIGHLOW) {
                    uint8_t *at = image + page_rva + ofs;
                    if (at + 4 > image + vsize)
                        continue;
                    coff::wr32(at, coff::rd32(at) + delta);
                }
                // other types ignored
            }
            off += block_size;
        }
    } else {
        // Object-style: per-section COFF relocs resolved against the symbol
        // table. Section symbols (storage class 3, value = offset within the
        // section) dominate for shasm output.
        const uint8_t *symtab = file + out_hdr->fh.symtab_offset;
        for (uint32_t i = 0; i < out_hdr->section_count; i++) {
            const coff::SectionHeader &sh = out_hdr->sections[i];
            if (sh.nrelocs == 0)
                continue;
            for (uint16_t r = 0; r < sh.nrelocs; r++) {
                const uint32_t ro = sh.reloc_offset + r * 10;
                if (ro + 10 > file_size)
                    break;
                const uint32_t rel_off  = coff::rd32(file + ro);
                const uint16_t symindex = coff::rd16(file + ro + 4);
                const uint16_t rtype    = coff::rd16(file + ro + 6);

                // Walk the symbol table (skipping aux records) for ours.
                uint32_t aux = 0;
                uint32_t sym_value = 0;
                int32_t sym_section = 0;
                bool found = false;
                for (uint32_t si = 0; si < out_hdr->fh.nsymbols;
                     si += 1 + aux) {
                    if (out_hdr->fh.symtab_offset + (si + 1) * 18 > file_size)
                        break;
                    const uint8_t *e = symtab + si * 18;
                    aux = e[17]; // naux
                    if (si == symindex) {
                        sym_value = coff::rd32(e + 8);
                        sym_section =
                            static_cast<int32_t>(coff::rd16(e + 12));
                        found = true;
                        break;
                    }
                }
                if (!found)
                    continue;

                // r_vaddr is the offset from the beginning of the section
                // (PE spec, object-file relocations).
                uint8_t *at = image + sh.rva + rel_off;
                if (at + 4 > image + vsize)
                    continue;

                // Symbol VA = load base of the symbol's section + value.
                uint32_t sym_va =
                    reinterpret_cast<uint32_t>(image) + sym_value;
                if (sym_section >= 1 &&
                    static_cast<uint32_t>(sym_section) <=
                        out_hdr->section_count) {
                    sym_va = reinterpret_cast<uint32_t>(image) +
                             out_hdr->sections[sym_section - 1].rva +
                             sym_value;
                }

                if (rtype == coff::REL_DIR32) {
                    coff::wr32(at, coff::rd32(at) + sym_va);
                } else if (rtype == coff::REL_REL32) {
                    coff::wr32(at, coff::rd32(at) + sym_va -
                                       (reinterpret_cast<uint32_t>(at) + 4));
                }
                // other types ignored
            }
        }
    }

    flush_image(image, vsize);

    *out_image = image;
    *err = "ok";
    return 0;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
constexpr int kMaxFiles = 12;
constexpr int kMaxName = 25;

static char_const16_t g_find_pattern[] = {
    '\\', 'f', 'l', 's', '0', '\\', '*', '.', 'N', 'T', 'X', 0,
};

static void banner(void) {
    fillScreen(color(0, 0, 40));
    Debug_SetCursorPosition(0, 0);
    Debug_PrintString("NTOS 0.1 - SH4BE COFF loader", true);
    Debug_SetCursorPosition(0, 1);
    Debug_PrintString("Select an .NTX (big-endian COFF EXE)", false);
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

    banner();

    // Scan flash for .NTX payloads. File_FindFirst returns UCS-16 names;
    // File_Open takes ANSI paths, so convert on the way in.
    char names[kMaxFiles][kMaxName + 1] = {};
    uint32_t sizes[kMaxFiles] = {};
    int count = 0;

    {
        int find = 0;
        char_const16_t name[32] = {};
        File_FindInfo info{};
        if (File_FindFirst(g_find_pattern, &find, name, &info) == 0) {
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

    if (count == 0) {
        Debug_SetCursorPosition(0, 3);
        Debug_PrintString("No .NTX files found on \\fls0.", false);
        Debug_SetCursorPosition(0, 4);
        Debug_PrintString("Put a BE COFF EXE there and retry.", false);
        LCD_Refresh();
        Debug_WaitKey();
        return 0;
    }

    for (int i = 0; i < count; i++) {
        char line[48];
        char sz[12];
        tiny_utoa(sizes[i], sz);
        int p = 0;
        line[p++] = static_cast<char>('0' + i);
        line[p++] = ':';
        line[p++] = ' ';
        for (int k = 0; k < kMaxName && names[i][k] != '\0'; k++)
            line[p++] = names[i][k];
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
                return 0;
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
        for (int k = 0; k < kMaxName && names[selected][k] != '\0'; k++)
            path[p++] = names[selected][k];
        path[p] = '\0';
    }

    int fd = File_Open(path, FILE_OPEN_READ);
    if (fd < 0) {
        ntos_puts("File_Open failed");
        Debug_WaitKey();
        return 1;
    }
    uint32_t fsize = sizes[selected];
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

    // Load it.
    coff::Image hdr{};
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
    Mem_Free(file);

    Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0,
                 "Loaded at 0x%08lX entry 0x%08lX",
                 static_cast<unsigned long>(reinterpret_cast<uintptr_t>(image)),
                 static_cast<unsigned long>(reinterpret_cast<uintptr_t>(image)) +
                     hdr.oh.entry_rva);
    LCD_Refresh();

    // Execute. Entry contract: r4 = &NtosApi, r5 = argv, r6 = envp.
    using entry_fn = int (*)(const NtosApi *, char **, char **);
    entry_fn entry = reinterpret_cast<entry_fn>(
        reinterpret_cast<uint32_t>(image) + hdr.oh.entry_rva);

    int ret = entry(&g_ntos_api, nullptr, nullptr);

    Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0,
                 "Payload returned 0x%08lX",
                 static_cast<unsigned long>(static_cast<uint32_t>(ret)));
    LCD_Refresh();
    Mem_Free(image);
    Debug_WaitKey();
    return ret;
}
