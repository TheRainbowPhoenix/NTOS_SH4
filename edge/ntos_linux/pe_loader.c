/* pe_loader.c — minimal PE32/PE32+ loader for NTOS_SH4_linux.
 *
 * Maps x86-64 PE payload DLLs (zig cc -target x86_64-windows-gnu) into the
 * host process, applies base relocations when the preferred base is taken,
 * parses export directories and exposes NTOS-style IAT-slot patching:
 * payload sources declare imports as `const void *volatile __imp_<name>`
 * (payload/ntimport.h). On device elf2ntx.py turns those slots into a PE
 * import directory resolved against COREDLL.DLL; here the loader finds the
 * exported __imp_* slots of every module and writes the resolved builtin or
 * cross-module function address into them, which is the same "loader snaps
 * the IAT" model without needing an import directory at all.
 */
#include "ntos_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef struct {
    uint16_t e_magic;
    uint16_t cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs, lfarlc, ovno;
    uint16_t res[4];
    int16_t oemid, oeminfo;
    uint16_t res2[10];
    int32_t lfanew;
} __attribute__((packed)) DosHeader;

typedef struct {
    uint16_t Machine, NumberOfSections;
    uint32_t TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
    uint16_t SizeOfOptionalHeader, Characteristics;
} __attribute__((packed)) FileHeader;

typedef struct {
    uint16_t Magic;
    uint8_t MajorLinkerVersion, MinorLinkerVersion;
    uint32_t SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint, BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment, FileAlignment;
    uint16_t MajorOSVer, MinorOSVer, MajorImageVer, MinorImageVer,
        MajorSubsysVer, MinorSubsysVer, Win32VerValue;
    uint32_t SizeOfImage, SizeOfHeaders, CheckSum;
    uint16_t Subsystem, DllCharacteristics;
    uint64_t SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit;
    uint32_t LoaderFlags, NumberOfRvaAndSizes;
} __attribute__((packed)) OptHeader64;

typedef struct {
    uint16_t Machine, NumberOfSections;
    uint32_t TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
    uint16_t SizeOfOptionalHeader, Characteristics;
} __attribute__((packed)) CoffHeader;

typedef struct {
    char Name[8];
    uint32_t VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData;
    uint32_t PointerToRelocations, PointerToLinenumbers;
    uint16_t NumberOfRelocations, NumberOfLinenumbers;
    uint32_t Characteristics;
} __attribute__((packed)) SectionHeader;

#define DIR_EXPORT 0
#define DIR_BASERELOC 5

static void die(const char *msg)
{
    fprintf(stderr, "pe_loader: %s\n", msg);
}

int pe_load(PeModule *mod, const char *name, const uint8_t *data, size_t size)
{
    memset(mod, 0, sizeof(*mod));
    snprintf(mod->name, sizeof(mod->name), "%s", name);
    if (size < sizeof(DosHeader)) {
        die("file too small");
        return -1;
    }
    const DosHeader *dos = (const DosHeader *)data;
    if (dos->e_magic != 0x5a4d) {
        die("no MZ header");
        return -1;
    }
    size_t pe_off = (size_t)dos->lfanew;
    if (pe_off + 24 > size) {
        die("bad e_lfanew");
        return -1;
    }
    if (memcmp(data + pe_off, "PE\0\0", 4)) {
        die("no PE signature");
        return -1;
    }
    const FileHeader *fh = (const FileHeader *)(data + pe_off + 4);
    if (fh->Machine != 0x8664) {
        die("not x86-64 PE (only x86-64 payloads run natively on the Linux host)");
        return -1;
    }
    const OptHeader64 *oh = (const OptHeader64 *)(data + pe_off + 24);
    if (oh->Magic != 0x20b) {
        die("not PE32+");
        return -1;
    }
    mod->is_dll = !!(fh->Characteristics & 0x2000);
    mod->entry_rva = oh->AddressOfEntryPoint;
    mod->size = oh->SizeOfImage;

    /* Map SizeOfImage at the preferred base when free, else anywhere+reloc. */
    uint8_t *base = mmap((void *)(uintptr_t)oh->ImageBase, mod->size,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int need_reloc = 0;
    if (base == MAP_FAILED || (uintptr_t)base != oh->ImageBase) {
        if (base != MAP_FAILED)
            munmap(base, mod->size);
        base = mmap(NULL, mod->size, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED) {
            die("mmap failed");
            return -1;
        }
        need_reloc = 1;
    }
    mod->base = base;

    /* headers, then sections */
    memcpy(base, data, oh->SizeOfHeaders < size ? oh->SizeOfHeaders : size);
    const SectionHeader *sec = (const SectionHeader *)(data + pe_off + 24 + fh->SizeOfOptionalHeader);
    for (int i = 0; i < fh->NumberOfSections; ++i, ++sec) {
        if (!sec->VirtualAddress || !sec->VirtualSize)
            continue;
        uint8_t *dst = base + sec->VirtualAddress;
        size_t cp = sec->SizeOfRawData;
        if (sec->PointerToRawData + cp > size)
            cp = size > sec->PointerToRawData ? size - sec->PointerToRawData : 0;
        if (cp)
            memcpy(dst, data + sec->PointerToRawData, cp);
        /* rest already zero from MAP_ANONYMOUS */
    }

    if (need_reloc) {
        uint64_t delta = (uint64_t)(uintptr_t)base - oh->ImageBase;
        /* data directories live at opt+112 for PE32+ */
        const uint64_t *dirs = (const uint64_t *)((const uint8_t *)oh + 112);
        if (oh->NumberOfRvaAndSizes > DIR_BASERELOC && dirs[DIR_BASERELOC * 2]) {
            uint32_t start = (uint32_t)dirs[DIR_BASERELOC * 2];
            uint32_t total = (uint32_t)dirs[DIR_BASERELOC * 2 + 1];
            uint8_t *p = base + start;
            uint8_t *end = p + total;
            while (p + 8 <= end) {
                uint32_t page_rva = *(uint32_t *)p;
                uint32_t block_size = *(uint32_t *)(p + 4);
                if (block_size < 8 || p + block_size > end)
                    break;
                for (uint32_t o = 8; o + 2 <= block_size; o += 2) {
                    uint16_t e = *(uint16_t *)(p + o);
                    uint16_t type = e >> 12, off = e & 0xfff;
                    void *target = base + page_rva + off;
                    if (type == 10) { /* IMAGE_REL_BASED_DIR64 */
                        *(uint64_t *)target += delta;
                    } else if (type == 0) {
                        /* absolute — skip */
                    }
                }
                p += block_size;
                (void)page_rva;
            }
        } else {
            die("image needs relocation but has no .reloc");
            munmap(base, mod->size);
            return -1;
        }
    }

    /* exports */
    const uint64_t *dirs = (const uint64_t *)((const uint8_t *)oh + 112);
    if (oh->NumberOfRvaAndSizes > DIR_EXPORT && dirs[DIR_EXPORT * 2]) {
        uint32_t rva = (uint32_t)dirs[DIR_EXPORT * 2];
        const uint8_t *e = base + rva;
        mod->exp_ordinal_base = *(uint32_t *)(e + 16);
        mod->exp_count = *(uint32_t *)(e + 20);
        mod->exp_names_count = *(uint32_t *)(e + 24);
        uint32_t addr_rva = *(uint32_t *)(e + 28);
        uint32_t name_rva = *(uint32_t *)(e + 32);
        uint32_t ord_rva = *(uint32_t *)(e + 36);
        mod->exp_dll_name = (const char *)(base + *(uint32_t *)(e + 12));
        mod->exp_funcs = (const U32 *)(base + addr_rva);
        mod->exp_names = (const U32 *)(base + name_rva);
        mod->exp_ords = (const uint16_t *)(base + ord_rva);
        mod->exp_rva_min = rva;
        mod->exp_rva_max = rva + (uint32_t)dirs[DIR_EXPORT * 2 + 1];
    }
    return 0;
}

typedef MSABI int (*PeEntryFn)(void *, U32, void *);

int pe_call_entry(PeModule *mod, void *instance)
{
    if (!mod->entry_rva)
        return 1;
    PeEntryFn fn = (PeEntryFn)(mod->base + mod->entry_rva);
    return fn(instance, 1 /*DLL_PROCESS_ATTACH*/, NULL);
}

void *pe_resolve(PeModule *mod, const char *name)
{
    if (!mod->exp_names_count)
        return NULL;
    for (uint32_t i = 0; i < mod->exp_names_count; ++i) {
        const char *n = (const char *)(mod->base + mod->exp_names[i]);
        if (!strcmp(n, name)) {
            /* the ordinal table already holds EAT indices (ordinal - base) */
            uint32_t ord = mod->exp_ords[i];
            if (ord >= mod->exp_count)
                return NULL;
            uint32_t rva = mod->exp_funcs[ord];
            /* real forwarders are ASCII "dll.symbol" strings inside the
             * export directory; zig/lld over-reports the directory size, so
             * check the content instead of the byte range */
            if (rva >= mod->exp_rva_min && rva < mod->exp_rva_max) {
                const char *p = (const char *)(mod->base + rva);
                int printable = 1, dots = 0;
                for (const char *q = p; q < (const char *)mod->base + mod->exp_rva_max && *q; ++q) {
                    if (*q == '.')
                        ++dots;
                    else if (*q < 0x20 || *q > 0x7e) {
                        printable = 0;
                        break;
                    }
                    if (q - p > 64)
                        break;
                }
                if (printable && dots == 1)
                    return NULL; /* true forwarder */
            }
            return mod->base + rva;
        }
    }
    return NULL;
}

/* Patch every exported __imp_<name> slot of mod: resolve <name> from the
 * builtin COREDLL table or any loaded module export. Two passes are done by
 * the caller so cross-DLL order does not matter. */
int pe_patch_iat_slots(PeModule *mod, PeModule * const *others, size_t other_count)
{
    int patched = 0;
    for (uint32_t i = 0; i < mod->exp_names_count; ++i) {
        const char *n = (const char *)(mod->base + mod->exp_names[i]);
        if (strncmp(n, "__imp_", 6))
            continue;
        uint32_t ord = mod->exp_ords[i]; /* already an EAT index */
        if (ord >= mod->exp_count)
            continue;
        const void *volatile *slot =
            (const void *volatile *)(mod->base + mod->exp_funcs[ord]);
        const char *want = n + 6;
        void *fn = host_lookup_builtin(want);
        if (!fn) {
            for (size_t k = 0; k < other_count && !fn; ++k) {
                if (others[k] == mod)
                    continue;
                fn = pe_resolve(others[k], want);
            }
        }
        if (!fn) {
            fprintf(stderr, "pe_loader: unresolvable import %s!%s (slot left NULL)\n",
                    mod->name, want);
            continue;
        }
        *slot = fn;
        ++patched;
    }
    return patched;
}
