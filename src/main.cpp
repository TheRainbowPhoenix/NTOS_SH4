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
#include <sdk/os/input.h>
#include <sdk/os/mem.h>

#include "pe.hpp"
#include "pe_rebase.hpp"
#include "pe_pager.hpp"
#include "host_world.hpp"
#include "irq_handoff.hpp"
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

struct NtosFind { uint32_t attr,size; uint16_t name[128]; };
struct NtosApi {
    puts_fn puts;          // [0] console line output
    printf_fn printf_;     // [1] Debug_Printf passthrough
    refresh_fn refresh;    // [2] LCD_Refresh
    malloc_fn malloc_;     // [3] Mem_Malloc (12 MiB heap @ 0x8C280000)
    free_fn free_;         // [4] Mem_Free
    getkey_fn get_key;     // [5] blocking key read, packed
    void (*screen_clear)(uint32_t);                      // [6]
    void (*pixel)(uint32_t, uint32_t, uint32_t);         // [7]
    void (*draw_line)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t); // [8]
    void (*fill_rect)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t); // [9]
    uint32_t (*read_input)(uint32_t *, uint32_t *);      // [10]
    int32_t (*file_open)(const char *, uint32_t);        // [11]
    int32_t (*file_read)(int32_t, void *, uint32_t);     // [12]
    int32_t (*file_write)(int32_t, const void *, uint32_t); // [13]
    int32_t (*file_close)(int32_t);                      // [14]
    uint32_t (*read_event)(uint32_t *, uint32_t *, uint32_t *); // [15]
    void (*draw_text)(uint32_t, uint32_t, const char *); // [16] text at pixel pos
    int32_t (*file_size)(int32_t);                     // [17] fstat size
    int32_t (*file_seek)(int32_t,int32_t,uint32_t);    // [18]
    int32_t (*file_remove)(const char *);              // [19]
    int32_t (*file_rename)(const char *,const char *); // [20]
    int32_t (*file_mkdir)(const char *);               // [21]
    int32_t (*file_stat)(const char *);                // [22] exists
    int32_t (*page_hint)(uint32_t);                   // [23] private pager experiment
    int32_t (*find_first)(const char *,int32_t *,NtosFind *); // [24] directory search
    int32_t (*find_next)(int32_t,NtosFind *);                // [25]
    int32_t (*find_close)(int32_t);                         // [26]
};

static int cur_row = 0;

// Console output goes through Debug_Printf -- the small font (the big
// Debug_PrintString glyphs only fit ~28 columns and drown the screen).
// The SDK printer must receive a P1 resident string, never an untrusted
// pointer to a transient P0 page or a user stack after the MMU is switched.
static void ntos_puts(const char *s) {
    if (!s) return;
    char scratch[80];
    unsigned n = 0;
    for (; n + 1 < sizeof(scratch) && s[n]; ++n)
        scratch[n] = s[n];
    scratch[n] = 0;
    if (cur_row > 22) cur_row = 0;
    Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0,
                 "%s", scratch);
    LCD_Refresh();
}

// New fixed-width NTOS call gates; all arguments follow the SH4 scalar ABI.
// A COREDLL HDC is a synthetic handle, not an SDK device context.
static void ntos_screen_clear(uint32_t rgb565) {
    fillScreen(static_cast<uint16_t>(rgb565));
}
static void ntos_pixel(uint32_t x, uint32_t y, uint32_t rgb565) {
    if (x < width && y < height)
        setPixel(x, y, static_cast<uint16_t>(rgb565));
}
static void ntos_line(uint32_t x1, uint32_t y1, uint32_t x2,
                      uint32_t y2, uint32_t rgb565) {
    line(static_cast<int>(x1), static_cast<int>(y1),
         static_cast<int>(x2), static_cast<int>(y2),
         static_cast<uint16_t>(rgb565));
}
static void ntos_fillrect(uint32_t l, uint32_t t, uint32_t r,
                          uint32_t b, uint32_t rgb565) {
    if (l >= width || t >= height || r <= l || b <= t) return;
    if (r > width) r = width;
    if (b > height) b = height;
    for (uint32_t y = t; y < b; ++y)
        line(static_cast<int>(l), static_cast<int>(y),
             static_cast<int>(r - 1), static_cast<int>(y),
             static_cast<uint16_t>(rgb565));
}
// Return 1 for a touch, 2 for CLEAR, 0 for other events. Blocking by design.
static uint32_t ntos_input(uint32_t *x, uint32_t *y) {
    Input_Event event{};
    GetInput(&event, 0xFFFFFFFFu, 0x10);
    if (event.type == EVENT_TOUCH) {
        if (x) *x = static_cast<uint32_t>(event.data.touch_single.p1_x);
        if (y) *y = static_cast<uint32_t>(event.data.touch_single.p1_y);
        return 1;
    }
    if (event.type == EVENT_KEY &&
        event.data.key.keyCode == KEYCODE_POWER_CLEAR) return 2;
    return 0;
}

// File I/O is mediated by NTOS. Hollyhock OS routines must not be given
// P0-mapped payload pointers: the SDK may temporarily change the MMU state.
// Copy paths and data to/from a small P1-resident stack bounce buffer.
static bool kernel_path(char *out, const char *in) {
    if (!in) return false;
    uint32_t i=0;
    for (; i<95 && in[i]; ++i) out[i]=in[i];
    if (i==95) return false;
    out[i]=0;
    return true;
}
static int32_t ntos_file_open(const char *name, uint32_t flags) {
    char path[96];
    if (!kernel_path(path,name)) return -2;
    return File_Open(path,static_cast<int>(flags));
}
static int32_t ntos_file_read(int32_t fd, void *data, uint32_t count) {
    if (!data || count>4096u) return -2;
    auto *dst=static_cast<uint8_t *>(data);
    uint8_t bounce[256];
    uint32_t total=0;
    while (total<count) {
        uint32_t amount=count-total;
        if (amount>sizeof(bounce)) amount=sizeof(bounce);
        int n=File_Read(fd,bounce,static_cast<int>(amount));
        if (n<0) return total ? static_cast<int32_t>(total) : n;
        for (int i=0;i<n;++i) dst[total+static_cast<uint32_t>(i)]=bounce[i];
        total+=static_cast<uint32_t>(n);
        if (n<static_cast<int>(amount)) break;
    }
    return static_cast<int32_t>(total);
}
static int32_t ntos_file_write(int32_t fd,const void *data,uint32_t count) {
    if (!data || count>4096u) return -2;
    const auto *src=static_cast<const uint8_t *>(data);
    uint8_t bounce[256];
    uint32_t total=0;
    while (total<count) {
        uint32_t amount=count-total;
        if (amount>sizeof(bounce)) amount=sizeof(bounce);
        for (uint32_t i=0;i<amount;++i) bounce[i]=src[total+i];
        int n=File_Write(fd,bounce,static_cast<int>(amount));
        if (n<0) return total ? static_cast<int32_t>(total) : n;
        total+=static_cast<uint32_t>(n);
        if (n<static_cast<int>(amount)) break;
    }
    return static_cast<int32_t>(total);
}
static int32_t ntos_file_close(int32_t fd) { return File_Close(fd); }

// Structured input: kind 1=touch down/drag, 2=CLEAR, 3=key press.
// On key press x contains the Hollyhock KEYCODE_* value.
static uint32_t ntos_read_event(uint32_t *x, uint32_t *y,
                                 uint32_t *direction) {
    Input_Event event{};
    GetInput(&event, 0xFFFFFFFFu, 0x10);
    if (event.type == EVENT_TOUCH) {
        if (event.data.touch_single.direction != TOUCH_DOWN &&
            event.data.touch_single.direction != TOUCH_HOLD_DRAG) return 0;
        if (x) *x = static_cast<uint32_t>(event.data.touch_single.p1_x);
        if (y) *y = static_cast<uint32_t>(event.data.touch_single.p1_y);
        if (direction) *direction = static_cast<uint32_t>(event.data.touch_single.direction);
        return 1;
    }
    if (event.type == EVENT_KEY && event.data.key.direction == KEY_PRESSED) {
        if (event.data.key.keyCode == KEYCODE_POWER_CLEAR) return 2;
        if (x) *x = static_cast<uint32_t>(event.data.key.keyCode);
        if (y) *y = 0;
        if (direction) *direction = 0;
        return 3;
    }
    return 0;
}

// Fixed-argument text bridge for WinCE TextOutW. Debug_Printf receives
// only trusted P1 stack strings; coordinates are bounded to the LCD.
static void ntos_draw_text(uint32_t x, uint32_t y, const char *text) {
    if (!text || x >= static_cast<uint32_t>(width) ||
        y >= static_cast<uint32_t>(height)) return;
    char tmp[64];
    uint32_t n=0;
    while (n+1 < sizeof(tmp) && text[n]) {
        tmp[n]=text[n];
        ++n;
    }
    tmp[n]=0;
    Debug_Printf(x, y, false, 0, "%s", tmp);
}
static int32_t ntos_file_size(int32_t fd) {
    struct File_Stat st{};
    if (File_Fstat(fd, &st) < 0) return -1;
    return static_cast<int32_t>(st.fileSize);
}

static int32_t ntos_file_seek(int32_t fd,int32_t offset,uint32_t whence) {
    if (whence>2) return -2;
    return File_Lseek(fd,offset,static_cast<File_Whence>(whence));
}
static int32_t ntos_file_remove(const char *name) {
    char path[96];
    if (!kernel_path(path,name)) return -2;
    return File_Remove(path);
}
static int32_t ntos_file_rename(const char *from,const char *to) {
    char source[96],dest[96];
    if (!kernel_path(source,from) || !kernel_path(dest,to)) return -2;
    return File_Rename(source,dest);
}
static int32_t ntos_file_mkdir(const char *name) {
    char path[96];
    if (!kernel_path(path,name)) return -2;
    return File_Mkdir(path);
}
static int32_t ntos_file_stat(const char *name) {
    char path[96];
    if (!kernel_path(path,name)) return -2;
    struct File_Stat st{};
    return File_Stat(path,&st);
}

static uint32_t ntos_getkey(void) {
    uint32_t key1 = 0;
    uint32_t key2 = 0;
    getKey(&key1, &key2);
    return (key1 << 8) | (key2 & 0xFFu);
}

static int32_t ntos_page_hint(uint32_t va);

// Directory results are bounced through P1 just like file reads.
static void copy_find(NtosFind *out,const char_const16_t *name,const File_FindInfo &info) {
    out->attr=info.type==File_FindInfo::EntryTypeFile ? 0x20u : 0x10u;
    out->size=info.size;
    unsigned i=0;for(;i<127u && name[i];++i)out->name[i]=name[i];out->name[i]=0;
}
static int32_t ntos_find_first(const char *name,int32_t *handle,NtosFind *out) {
    if(!name || !handle || !out)return -2;
    char path[96];if(!kernel_path(path,name))return -2;
    char_const16_t pattern[96]{},found[260]{};
    for(unsigned i=0;path[i];++i)pattern[i]=static_cast<unsigned char>(path[i]);
    File_FindInfo info{};int search=0;
    int result=File_FindFirst(pattern,&search,found,&info);
    if(result==0){*handle=search;copy_find(out,found,info);}return result;
}
static int32_t ntos_find_next(int32_t handle,NtosFind *out) {
    if(!out)return -2;
    char_const16_t found[260]{};File_FindInfo info{};
    int result=File_FindNext(handle,found,&info);
    if(result==0)copy_find(out,found,info);
    return result;
}
static int32_t ntos_find_close(int32_t handle) {return File_FindClose(handle);}

static NtosApi g_ntos_api = {
    &ntos_puts,
    Debug_Printf, // SDK declares these as function pointers, not functions
    LCD_Refresh,
    &Mem_Malloc,  // this one really is an inline function in the SDK
    Mem_Free,
    &ntos_getkey,
    &ntos_screen_clear,  // [6] clear RGB565
    &ntos_pixel,         // [7] pixel RGB565
    &ntos_line,          // [8] line RGB565
    &ntos_fillrect,      // [9] filled rectangle RGB565
    &ntos_input,         // [10] touch/key event
    &ntos_file_open,     // [11] file open
    &ntos_file_read,     // [12] file read
    &ntos_file_write,    // [13] file write
    &ntos_file_close,    // [14] file close
    &ntos_read_event,    // [15] touch and key event
    &ntos_draw_text,     // [16] text at pixel location
    &ntos_file_size,     // [17] fstat size
    &ntos_file_seek,     // [18] seek
    &ntos_file_remove,   // [19] delete
    &ntos_file_rename,   // [20] rename
    &ntos_file_mkdir,    // [21] mkdir
    &ntos_file_stat,     // [22] stat
    &ntos_page_hint,     // [23] experiment: explicit software page prefault
    &ntos_find_first,    // [24] bounded WinCE finder bridge
    &ntos_find_next,     // [25]
    &ntos_find_close,    // [26]
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

    // Copy-load fallback and UTLB-mapped loader now use the SAME strict
    // WinCE-style fixup engine. PE block records are LE, pointer words
    // are SH4BE; invalid or unsupported fixups abort before execution.
    const char *reloc_error=nullptr;
    if (!ntos_rebase::apply(*out_hdr,image,vsize,
                reinterpret_cast<uint32_t>(image),&reloc_error)) {
        *err=reloc_error;
        Mem_Free(image);
        return -5;
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
    uint32_t base;   // assigned virtual base (may differ from PE ImageBase)
    uint32_t npages;
};

static void *align_page(void *p) {
    return reinterpret_cast<void *>(
        (reinterpret_cast<uint32_t>(p) + 0xFFFu) & ~0xFFFu);
}
#include "hardware_fault.hpp"

// This private NTOS API is a diagnostic only: a payload can ask the P1
// stage0 to install a known backing page BEFORE accessing it. Emulator
// fault delivery and genuine VBR exceptions are counted separately.
static int32_t ntos_page_hint(uint32_t va) {
#if NTOS_ENABLE_TLB_MISS_PAGER
    auto &ctx=ntos_hw::g_context;
    if(!ctx.installed || !mmu::at_enabled())return -1;
    ctx.hint_calls=ctx.hint_calls+1u;
    return ntos_hw::prime(va,false) ? 0 : -2;
#else
    (void)va;
    return -1;
#endif
}


// The eager mapper uses the exact same file-to-page materializer as the
// future demand pager. It runs with MMU translation disabled and does not
// install any exception vectors. The source is still buffered in P1 RAM;
// the upcoming fault backend will replace this Reader with safe file I/O.
struct PeMemoryReader {
    const uint8_t *bytes;
    uint32_t count;
};
static bool pe_memory_read(void *ctx,uint32_t pos,uint8_t *dst,uint32_t len) {
    auto *r=static_cast<PeMemoryReader *>(ctx);
    if(!r || !dst || pos>r->count || len>r->count-pos)
        return false;
    Mem_Memcpy(dst,r->bytes+pos,len);
    return true;
}
// Copy an image into 4 KiB-aligned heap memory without applying any
// relocations -- it will execute at its preferred base via the UTLB.
static int load_mapped(const uint8_t *file, uint32_t file_size,
                       const pe::Image &hdr, MappedImage *out_mi,
                       const char **err) {
    *err = "unknown";
    // The PE ImageBase is only a *preference*. Windows CE commonly links
    // every DLL to 0x10000000, outside our smaller P0 UTLB arena. The
    // allocator selects an appropriate SH7305 VA and applies .reloc later.
    if (hdr.oh.magic != pe::OPT_MAGIC_PE32 || hdr.oh.image_base == 0) {
        *err = "no valid PE32 preferred image base";
        return -1;
    }
    uint32_t vsize = hdr.oh.size_of_image;
    if (vsize == 0 || vsize > 8u * 1024u * 1024u)
        vsize = (hdr.image_end + 0xFFFu) & ~0xFFFu;
    if (vsize == 0) {
        *err = "empty image";
        return -1;
    }
    // Preferred-base conflicts (including the API window) are resolved
    // by choose_base(). Do not reject relocatable images here.

    // Normal host mode maps every page eagerly; experimental VBR mode
    // retains all physical RAM backing but faults virtual pages on demand.
#if !NTOS_ENABLE_TLB_MISS_PAGER
    if (vsize > (mmu::SLOT_ROTATING_LAST+1u)*0x1000u) {
        *err="PE too large for 55-page UTLB budget (needs pager)";
        return -1;
    }
#endif
    // The UTLB maps complete 4 KiB pages even when PE SizeOfImage is not
    // page-aligned (e.g. COREDLL). Reserve/zero the full final page.
    const uint32_t mapped_bytes=(vsize+0xFFFu)&~0xFFFu;
    if (mapped_bytes<vsize || mapped_bytes>8u*1024u*1024u+0x1000u) {
        *err="PE image size overflows mapped pages";
        return -1;
    }
    auto *raw=static_cast<uint8_t *>(Mem_Malloc(mapped_bytes+0xFFFu));
    if (raw == nullptr) {
        *err = "out of memory";
        return -2;
    }
    auto *image = static_cast<uint8_t *>(align_page(raw));
    Mem_Memset(image, 0, mapped_bytes);

    // Populate each page from file-backed sections plus zero-fill,
    // using the read-at backend that will also serve future demand faults.
    PeMemoryReader reader{file,file_size};
    ntos_pager::Source src{&hdr,&reader,&pe_memory_read,file_size,
                            hdr.oh.image_base,true};
    for(uint32_t page=0u;page<mapped_bytes/0x1000u;++page) {
        if(!ntos_pager::materialize(src,page,
                 image+page*0x1000u,err)) {
            Mem_Free(raw);
            return -3;
        }
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
struct LinkedModule {
    const char *name;
    const pe::Image *hdr;
    const MappedImage *mapping;
};
static bool module_matches(const char *a, const char *b) {
    if (!a || !b) return false;
    for (; *a && *b; ++a, ++b) {
        char x=*a, y=*b;
        if (x>='a' && x<='z') x=static_cast<char>(x-32);
        if (y>='a' && y<='z') y=static_cast<char>(y-32);
        if (x != y) return false;
    }
    return *a == *b;
}
// Resolve WinCE forwarder chains such as OTHERDLL.Func and OTHERDLL.#7,
// including the .DLL suffix when omitted. Only the DLLs registered in the
// module graph are eligible. The caller discovers indirect dependencies
// before starting import binding. Depth bounds prevent cyclic forwarders.
static bool resolve_export_chain(const LinkedModule *modules,
                                 unsigned module_count,
                                 const LinkedModule *start,
                                 const char *symbol,uint32_t ordinal,
                                 bool by_ordinal,uint32_t *address,
                                 const char **err) {
    const LinkedModule *target=start;
    const char *name=symbol;
    char forward_name[128]{};
    for(unsigned depth=0;depth<12u;++depth) {
        if(!target || !target->mapping || !target->mapping->image) {
            *err="forwarded PE dependency not loaded";
            return false;
        }
        uint32_t function_rva=0u;
        if(!pe::export_address(*target->hdr,target->mapping->image,
                               target->mapping->vsize,name,ordinal,
                               by_ordinal,&function_rva)) {
            *err="named/ordinal WinCE export missing";return false;
        }
        const uint32_t export_begin=target->hdr->oh.export_rva;
        const uint32_t export_len=target->hdr->oh.export_size;
        if(function_rva<export_begin ||
           function_rva-export_begin>=export_len) {
            *address=target->mapping->base+function_rva;
            return true;
        }
        if(!pe::rva_string(target->mapping->image,
                           target->mapping->vsize,function_rva,
                           forward_name,sizeof(forward_name))) {
            *err="truncated WinCE forwarded export";return false;
        }
        unsigned split=0u;
        for(unsigned i=0u;forward_name[i];++i)
            if(forward_name[i]=='.')split=i;
        if(!split || !forward_name[split+1u]) {
            *err="malformed forwarded PE symbol";return false;
        }
        // Forwarder string may use "FOO.Bar" or "FOO.DLL.Bar".
        char dependency[40]{};
        if(split>=sizeof(dependency)-5u) {
            *err="forwarded DLL filename too long";return false;
        }
        unsigned n=0u;
        for(;n<split;++n)dependency[n]=forward_name[n];
        dependency[n]=0;
        if(n<4u || !module_matches(dependency+n-4u,".DLL")) {
            dependency[n++]='.';dependency[n++]='D';
            dependency[n++]='L';dependency[n++]='L';
            dependency[n]=0;
        }
        target=nullptr;
        for(unsigned i=0;i<module_count;++i)
            if(module_matches(modules[i].name,dependency)) {
                target=&modules[i];break;
            }
        if(!target) {
            *err="forwarder DLL missing from dependency graph";
            return false;
        }
        name=forward_name+split+1u;
        if(name[0]=='#') {
            uint32_t value=0u;
            if(!name[1]) {
                *err="invalid forwarded ordinal";return false;
            }
            for(unsigned i=1u;name[i];++i) {
                if(name[i]<'0' || name[i]>'9' ||
                   value>65535u/10u) {
                    *err="invalid forwarded ordinal";return false;
                }
                value=value*10u+static_cast<uint32_t>(name[i]-'0');
                if(value>65535u) {
                    *err="invalid forwarded ordinal";return false;
                }
            }
            ordinal=value;by_ordinal=true;
        } else {
            by_ordinal=false;
        }
    }
    *err="cyclic or overly deep PE forwarder chain";
    return false;
}

// Resolve each PE import descriptor against its OWN named DLL. Import
// directories are LE; patched SH4 .idata pointers must be written BE.
static int snap_imports(const pe::Image &image_hdr, const MappedImage *image,
                        const LinkedModule *modules, unsigned module_count,
                        char *scratch, uint32_t scratch_size,
                        const char **err) {
    int total=0;
    for (uint32_t di=0;;++di) {
        pe::ImportDesc d{};
        if (!pe::read_import_desc(image_hdr,image->image,image->vsize,di,&d))
            break;
        // In a stock WinCE PE, OriginalFirstThunk may be zero: the
        // pre-binding hints/names then reside in FirstThunk itself.
        if (!d.name_rva || !d.ft_rva ||
            !pe::rva_string(image->image,image->vsize,d.name_rva,
                            scratch,scratch_size)) {
            *err="invalid module import descriptor";
            return -1;
        }
        const LinkedModule *target=nullptr;
        for (unsigned mi=0;mi<module_count;++mi)
            if (module_matches(scratch,modules[mi].name)) {
                target=&modules[mi];break;
            }
        if (!target || !target->mapping || !target->mapping->image) {
            *err="missing import module (install dependency DLL)";
            return -2;
        }
        for (uint32_t i=0;;++i) {
            // PE metadata is LE even for SH4BE code and data. The
            // import address slots are written as SH4BE pointer words.
            const uint32_t oft_base=d.oft_rva ? d.oft_rva:d.ft_rva;
            if(i>image->vsize/4u || i>0x4000u ||
               oft_base>image->vsize ||
               d.ft_rva>image->vsize ||
               i*4u>image->vsize-oft_base ||
               i*4u>image->vsize-d.ft_rva ||
               4u>image->vsize-oft_base-i*4u ||
               4u>image->vsize-d.ft_rva-i*4u) {
                *err="truncated PE thunk array";return -4;
            }
            const uint32_t oft=oft_base+i*4u;
            const uint32_t ft=d.ft_rva+i*4u;
            const uint32_t entry=pe::rd32le(image->image+oft);
            if(!entry)break;
            bool ordinal_import=(entry&0x80000000u)!=0u;
            const uint32_t ordinal=entry&0xffffu;
            if(!ordinal_import) {
                if(entry>=image->vsize ||
                   2u>image->vsize-entry ||
                   !pe::rva_string(image->image,image->vsize,
                                   entry+2u,scratch,scratch_size)) {
                    *err="invalid import-by-name entry";return -6;
                }
            }
            uint32_t absolute=0u;
            if(!resolve_export_chain(modules,module_count,target,
                                     ordinal_import ? nullptr:scratch,
                                     ordinal,ordinal_import,
                                     &absolute,err))
                return -7;
            pe::wr32be(image->image+ft,absolute);
            ++total;
        }
    }
    *err="ok";
    return total;
}

// Full UTLB-mapped run. Returns 0 on success (payload return value in
// *out_ret) or -1 on loader error with `err` set. On every exit path the
// MMU is disabled, the UTLB flushed and all allocations released.
// The on-device module graph is intentionally bounded by the physical UTLB
// capacity. Discovery is recursive, by the *actual PE import descriptor name*;
// it is no longer a hardcoded COREDLL + optional XAMLUI pair.
constexpr unsigned NTOS_MAX_MODULES=16u;  // EXE + fifteen distinct DLLs
constexpr uint32_t NTOS_MAX_FILE_BYTES=8u*1024u*1024u;
struct ModuleNode {
    char name[40];
    pe::Image hdr;
    MappedImage mapped;
    uint8_t *file;
    uint32_t file_bytes;
};

static bool safe_dll_name(const char *name) {
    if (!name) return false;
    uint32_t n=0;
    for(;name[n] && n<38u;++n) {
        const char c=name[n];
        // Reject path separators, drive syntax, spaces and traversal.
        // DLL files are resolved within \fls0, not arbitrary SDK paths.
        if (!((c>='A' && c<='Z') || (c>='a' && c<='z') ||
              (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))
            return false;
        if (n>0 && name[n-1]=='.' && c=='.') return false;
    }
    if(n<5u || n>38u || name[n]) return false;
    const char *suffix=name+n-4u;
    return module_matches(suffix,".DLL");
}

static bool dll_path(char *dst,uint32_t capacity,const char *name) {
    static const char root[]="\\fls0\\";
    if (!safe_dll_name(name)) return false;
    uint32_t pos=0;
    for(unsigned i=0;root[i];++i) {
        if(pos+1u>=capacity) return false;
        dst[pos++]=root[i];
    }
    for(unsigned i=0;name[i];++i) {
        if(pos+1u>=capacity) return false;
        dst[pos++]=name[i];
    }
    dst[pos]=0;
    return true;
}
static bool read_module_file(const char *path,uint8_t **dst,
                             uint32_t *bytes,const char **error) {
    *dst=nullptr;*bytes=0u;
    int fd=File_Open(path,FILE_OPEN_READ);
    if(fd<0){ *error="missing dependency DLL on fls0";return false; }
    struct File_Stat st{};
    if(File_Fstat(fd,&st)<0 || st.fileSize<512u ||
       st.fileSize>NTOS_MAX_FILE_BYTES) {
        *error="invalid DLL length or exceeds staging file limit";
        File_Close(fd);return false;
    }
    const uint32_t n=static_cast<uint32_t>(st.fileSize);
    uint8_t *buf=static_cast<uint8_t *>(Mem_Malloc(n));
    if(!buf) {
        *error="out of memory staging DLL";
        File_Close(fd);return false;
    }
    uint32_t off=0u;
    while(off<n) {
        uint32_t wanted=n-off;
        if(wanted>4096u) wanted=4096u;
        int count=File_Read(fd,buf+off,static_cast<int>(wanted));
        if(count<=0 || static_cast<uint32_t>(count)>wanted) break;
        off+=static_cast<uint32_t>(count);
    }
    File_Close(fd);
    if(off!=n) {
        Mem_Free(buf);*error="short read staging dependency";return false;
    }
    *dst=buf;*bytes=n;return true;
}

static bool require_dependency(const char *dependency,ModuleNode *nodes,
                               unsigned *count,const char **err) {
    if(!safe_dll_name(dependency)) {
        *err="invalid DLL dependency filename";return false;
    }
    for(unsigned k=1u;k<*count;++k)
        if(module_matches(dependency,nodes[k].name))
            return true;
    if(*count>=NTOS_MAX_MODULES) {
        *err="module graph exceeds loader registry capacity";return false;
    }
    ModuleNode &module=nodes[*count];
    unsigned j=0u;
    while(dependency[j]) {module.name[j]=dependency[j];++j;}
    module.name[j]=0;
    char path[64];
    if(!dll_path(path,sizeof(path),module.name)) {
        *err="invalid DLL filename";return false;
    }
    if(!read_module_file(path,&module.file,&module.file_bytes,err))
        return false;
    if(pe::parse(module.file,module.file_bytes,&module.hdr)!=0 ||
       !(module.hdr.fh.characteristics&pe::F_DLL)) {
        *err="dependency is not a valid SH4BE PE DLL";return false;
    }
    if(load_mapped(module.file,module.file_bytes,module.hdr,
                   &module.mapped,err)!=0)
        return false;
    ++*count;
    return true;
}

// Full eager-mapped implementation of the recursively discovered PE import
// graph. A later pager can replace the mapping backend without changing
// module discovery, base allocation or name/ordinal import resolution.
//
// This code deliberately does NOT enable the interrupt/pager prototypes:
// without an installed privileged TLB-miss handler, a nonresident code page
// would hang or soft-reset the calculator. The current limit is 55 pages.
static int run_mapped(uint8_t *file, uint32_t fsize, const pe::Image &hdr,
                      const char *core_path, uint32_t core_size,
                      const char *extra_path, uint32_t extra_size,
                      int *out_ret, const char **err) {
    (void)core_path;(void)core_size;(void)extra_path;(void)extra_size;
    *err="unknown";
    const host_world::Snapshot host_state=host_world::capture();
    ntos_irq::Owner irq_owner{};
    // Stage0 records ownership intent without installing an ISR. The host
    // retains VBR/UBC interrupts and the TLB miss path (gated OFF).
#if !NTOS_ENABLE_TLB_MISS_PAGER
    irq_owner.prepare(host_state,ntos_irq::Hooks{});
#endif
    if((host_state.mmucr&1u)!=0u) {
        *err="host MMU is already active";return -1;
    }
    ModuleNode nodes[NTOS_MAX_MODULES]={};
    LinkedModule exports[NTOS_MAX_MODULES]={};
    ntos_rebase::Region reservations[NTOS_MAX_MODULES]={};
    unsigned count=1u;
    uint32_t image_pages=0u;
#if NTOS_ENABLE_TLB_MISS_PAGER
    bool eager_experiment=false;
#endif
    uint8_t *api_raw=nullptr;
    uint8_t *api_block=nullptr;
    int rc=-1;

    nodes[0].hdr=hdr;
    nodes[0].file=file;
    nodes[0].file_bytes=fsize;
    // Main EXE is owned by the caller; only dependency buffers are freed.
    if(load_mapped(file,fsize,hdr,&nodes[0].mapped,err)!=0)
        goto done;

    // Breadth-first walk of the transitive DLL import graph. A module is
    // entered into the registry BEFORE its imports are scanned, so
    // A->B->A cycles and duplicate import descriptors cannot recurse forever.
    for(unsigned index=0u;index<count;++index) {
        const ModuleNode &requester=nodes[index];
        if(requester.hdr.oh.import_rva) {
            if(requester.hdr.oh.import_size<20u ||
               requester.hdr.oh.import_size/20u>256u) {
                *err="invalid or oversized PE import directory";goto done;
            }
            bool terminated=false;
            for(uint32_t di=0u;di<requester.hdr.oh.import_size/20u;++di) {
                pe::ImportDesc d{};
                if(!pe::read_import_desc(requester.hdr,
                                         requester.mapped.image,
                                         requester.mapped.vsize,di,&d)) {
                    terminated=true;break;
                }
                char dependency[40]={};
                if(!pe::rva_string(requester.mapped.image,
                                   requester.mapped.vsize,d.name_rva,
                                   dependency,sizeof(dependency)) ||
                   !require_dependency(dependency,nodes,&count,err))
                    goto done;
            }
            if(!terminated) {
                *err="unterminated PE dependency descriptor array";
                goto done;
            }
        }
        // WinCE also loads forwarded-export targets. Discover their named
        // target DLLs before import snapping; this handles forwarders even
        // when the target isn't named in the EXE's import directory.
        if(index==0u || !requester.hdr.oh.export_rva)continue;
        pe::ExportDir ed{};
        if(!pe::read_export_dir(requester.hdr,requester.mapped.image,
                                requester.mapped.vsize,&ed)) {
            *err="malformed PE dependency export directory";goto done;
        }
        const uint32_t export_first=requester.hdr.oh.export_rva;
        const uint32_t export_bytes=requester.hdr.oh.export_size;
        for(uint32_t i=0u;i<ed.nfuncs;++i) {
            uint32_t function=pe::rd32le(
                requester.mapped.image+ed.addrfuncs_rva+i*4u);
            if(function<export_first ||
               function-export_first>=export_bytes)continue;
            char forward[128]{};
            if(!pe::rva_string(requester.mapped.image,
                               requester.mapped.vsize,function,
                               forward,sizeof(forward))) {
                *err="invalid forwarded export string";goto done;
            }
            unsigned dot=0u;
            for(unsigned k=0u;forward[k];++k)
                if(forward[k]=='.')dot=k;
            if(!dot || !forward[dot+1u] ||
               dot>=35u) {
                *err="malformed forwarded DLL name";goto done;
            }
            char dependency[40]{};
            unsigned n=0u;
            for(;n<dot;++n)dependency[n]=forward[n];
            dependency[n]=0;
            if(n<4u || !module_matches(dependency+n-4u,".DLL")) {
                dependency[n++]='.';dependency[n++]='D';
                dependency[n++]='L';dependency[n++]='L';
                dependency[n]=0;
            }
            if(!require_dependency(dependency,nodes,&count,err))
                goto done;
        }
    }

    // For <=55-page graphs, the experimental image uses the same fully
    // resident bootstrap as the proven regular loader. Unhinted small
    // programs (CHAINTEST, GUI) must not depend on an emulator delivering
    // missing-P0 access exceptions. Only larger graphs use the demand
    // pager, still requiring genuine VBR delivery or diagnostic hints.
    for(unsigned i=0u;i<count;++i)
        image_pages+=nodes[i].mapped.npages;
#if NTOS_ENABLE_TLB_MISS_PAGER
    eager_experiment=(image_pages<=mmu::SLOT_ROTATING_LAST+1u);
#endif
    // Reserve all virtual ranges before applying any relocation. The same
    // allocator handles DLLs sharing ImageBase, including 0x10000000.
    for(unsigned i=0u;i<count;++i) {
        uint32_t actual=0;
#if NTOS_ENABLE_TLB_MISS_PAGER
        if(eager_experiment
           ? !ntos_rebase::choose_base(nodes[i].hdr.oh.image_base,
                                      nodes[i].mapped.npages,
                                      reservations,i,&actual)
           : !ntos_fault::choose_virtual(nodes[i].hdr.oh.image_base,
                                        nodes[i].mapped.npages,
                                        reservations,i,&actual)) {
#else
        if(!ntos_rebase::choose_base(nodes[i].hdr.oh.image_base,
                                      nodes[i].mapped.npages,
                                      reservations,i,&actual)) {
#endif
            *err="no UTLB-compatible address for PE dependency";
            goto done;
        }
        nodes[i].mapped.base=actual;
        reservations[i]={actual,nodes[i].mapped.npages};
    }
#if !NTOS_ENABLE_TLB_MISS_PAGER
    if(image_pages>mmu::SLOT_ROTATING_LAST+1u) {
        *err="DLL graph exceeds resident UTLB page budget";goto done;
    }
#endif
    for(unsigned i=0u;i<count;++i) {
        if(!ntos_rebase::apply(nodes[i].hdr,nodes[i].mapped.image,
                               nodes[i].mapped.vsize,nodes[i].mapped.base,err))
            goto done;
        exports[i]={i==0u ? "" : nodes[i].name,
                    &nodes[i].hdr,&nodes[i].mapped};
    }
    // Always resolve by the individual import descriptor's DLL name.
    // Forward dependencies, shared dependencies and cyclic DLL references
    // are supported because *all* modules are registered before snapping.
    {
        char symbol[96];
        int total=0;
        for(unsigned i=0u;i<count;++i) {
            if(!nodes[i].hdr.oh.import_rva) continue;
            int n=snap_imports(nodes[i].hdr,&nodes[i].mapped,
                               exports+1u,count-1u,symbol,
                               sizeof(symbol),err);
            if(n<0) goto done;
            total+=n;
        }
        Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                     "Linked %d import(s), %u DLL(s)",total,count-1u);
        LCD_Refresh();
    }
    api_raw=static_cast<uint8_t *>(Mem_Malloc(0x4000u+0xFFFu));
    if(!api_raw){*err="API mapping allocation failed";goto done;}
    api_block=static_cast<uint8_t *>(align_page(api_raw));
    Mem_Memset(api_block,0,0x4000u);
    *reinterpret_cast<void **>(api_block)=&g_ntos_api;

    for(unsigned i=0u;i<count;++i)
        flush_image(nodes[i].mapped.image,nodes[i].mapped.vsize);
    flush_image(api_block,0x4000u);

    // Regular mode installs every page. Experimental fault mode installs
    // only the API window and each DLL's first page and EXE entry page;
    // the VBR+0x400 trampoline installs the remaining pages on demand.
    mmu::disable_at_flush();
#if NTOS_ENABLE_TLB_MISS_PAGER
    {
        ntos_irq::Hooks callbacks=ntos_hw::hooks(host_state);
        if(!irq_owner.prepare(host_state,callbacks)) {
            *err="cannot prepare IRQ handoff";goto done;
        }
        // Four API pages occupy UTLB slots 55..58, outside the pager's
        // 0..54 range. The SH7305 UTLB has no virtual-set/way collisions.
        for(uint32_t p=0u;p<mmu::API_WINDOW_PAGES;++p) {
            const uint32_t va=mmu::VA_API_WINDOW+p*0x1000u;
            const uint32_t pa=mmu::phys_addr(api_block+p*0x1000u);
            mmu::map_page_at_index(va,pa,mmu::SLOT_API_FIRST+p,0u);
#if NTOS_VERIFY_UTLB_SLOTS
            if(!mmu::verify_slot(mmu::SLOT_API_FIRST+p,va,pa)) {
                *err="SH7305 P4 UTLB API slot readback mismatch";
                goto done;
            }
#endif
        }
        for(unsigned i=0u;i<count;++i)
            if(!ntos_hw::add_module(nodes[i].mapped.base,
                                    nodes[i].mapped.image,
                                    nodes[i].mapped.npages)) {
                *err="cannot register virtual DLL backing";goto done;
            }
        if(eager_experiment) {
            // Match the normal loader: all reachable code, data and IAT
            // pages are resident before the first P0 instruction executes.
            // The fixed-slot policy allows all pages to be pinned here.
            for(unsigned i=0u;i<count;++i) {
                const MappedImage &m=nodes[i].mapped;
                for(uint32_t p=0u;p<m.npages;++p)
                    if(!ntos_hw::prime(m.base+p*0x1000u,true)) {
                        *err="cannot install eager experimental page";
                        goto done;
                    }
            }
        } else {
            // Oversized graphs need the experimental miss path. Only their
            // entry/bootstrap pages are pinned to leave eviction capacity.
            for(unsigned i=0u;i<count;++i)
                if(!ntos_hw::prime(nodes[i].mapped.base,true)) {
                    *err="no UTLB way for DLL bootstrap page";goto done;
                }
            if(!ntos_hw::prime(nodes[0].mapped.base+hdr.oh.entry_rva,true)) {
                *err="no UTLB way for EXE entry";goto done;
            }
        }
        // Never call Hollyhock Debug_Printf/LCD_Refresh while our
        // experimental VBR+0x400 exception handler owns VBR. SDK output
        // can temporarily switch MMU/interrupt worlds behind our back.
        if(!irq_owner.acquire()) {
            *err="VBR exception ownership install failed";goto done;
        }
    }
#else
    {
        // A SH7305 UTLB is fully associative: map at fixed slot numbers,
        // exactly as CP400 OS_02.01.7002 mmu_replace_entry_4k() does.
        uint32_t slot=0u;
        for(unsigned i=0u;i<count;++i) {
            const MappedImage &m=nodes[i].mapped;
            for(uint32_t p=0u;p<m.npages;++p,++slot)
                mmu::map_page_at_index(m.base+p*0x1000u,
                    mmu::phys_addr(m.image+p*0x1000u),slot,0u);
        }
        for(uint32_t p=0u;p<mmu::API_WINDOW_PAGES;++p)
            mmu::map_page_at_index(
                mmu::VA_API_WINDOW+p*0x1000u,
                mmu::phys_addr(api_block+p*0x1000u),
                mmu::SLOT_API_FIRST+p,0u);
    }
#endif

    mmu::enable_at();
#if !NTOS_ENABLE_TLB_MISS_PAGER
    for(unsigned i=0u;i<count;++i)
        mmu::cache_flush_invalidate_block(
             reinterpret_cast<void *>(nodes[i].mapped.base),
             nodes[i].mapped.vsize);
    mmu::cache_flush_invalidate_block(
        reinterpret_cast<void *>(mmu::VA_API_WINDOW),0x4000u);
#endif

    // WinCE loader invokes each DLL entry after its dependencies have
    // been relocated and imports snapped, and before EXE entry. Reverse
    // BFS gives a dependency-first order for the acyclic part of the graph;
    // cyclic DLLs are supported for imports, but their init order remains
    // deterministic rather than arbitrarily re-entrant. On failure,
    // detach the DLLs that already accepted PROCESS_ATTACH.
    {
        using dll_entry=int (*)(uint32_t,uint32_t,void *);
        using exe_entry=int (*)(const NtosApi *,char **,char **);
        bool attached[NTOS_MAX_MODULES]={};
        bool attach_ok=true;
        // DLL_PROCESS_ATTACH=1, DLL_PROCESS_DETACH=0 from WinCE SDK.
        for(unsigned i=count;i-->1u;) {
            const auto &mod=nodes[i];
            if(!mod.hdr.oh.entry_rva)continue;
            dll_entry entry=reinterpret_cast<dll_entry>(
                mod.mapped.base+mod.hdr.oh.entry_rva);
            if(!entry(mod.mapped.base,1u,nullptr)) {
                attach_ok=false;
                *err="DLL_PROCESS_ATTACH returned FALSE";
                break;
            }
            attached[i]=true;
        }
        if(attach_ok) {
            exe_entry entry=reinterpret_cast<exe_entry>(
                nodes[0].mapped.base+hdr.oh.entry_rva);
            *out_ret=entry(&g_ntos_api,nullptr,nullptr);
            *err="ok";
            rc=0;
        }
        // Opposite of attach order: release all initialized modules.
        for(unsigned i=1u;i<count;++i) {
            if(!attached[i])continue;
            const auto &mod=nodes[i];
            dll_entry entry=reinterpret_cast<dll_entry>(
                mod.mapped.base+mod.hdr.oh.entry_rva);
            (void)entry(mod.mapped.base,0u,nullptr);
        }
    }

done:
    mmu::disable_at_flush();
    irq_owner.release();
    host_world::restore(host_state);
#if NTOS_ENABLE_TLB_MISS_PAGER
    // All host UI work occurs AFTER restoring the original VBR and SR.
    // Report the actual fault/eviction count when emulator testing PAGERTEST.
    Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                 "PF=%lu HINT=%lu EV=%lu BAD=%lu",
                 static_cast<unsigned long>(ntos_hw::g_context.hw_faults),
                 static_cast<unsigned long>(ntos_hw::g_context.hint_calls),
                 static_cast<unsigned long>(ntos_hw::g_context.table.evictions),
                 static_cast<unsigned long>(ntos_hw::g_context.fault_error));
    if(ntos_hw::g_context.hw_faults!=0u) {
        Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                     "TEA=%08lX EV=%03lX SPC=%08lX",
                     static_cast<unsigned long>(ntos_hw::g_context.last_tea),
                     static_cast<unsigned long>(ntos_hw::g_context.last_expevt),
                     static_cast<unsigned long>(ntos_hw::g_context.last_spc));
        Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                     "SSR=%08lX MMUCR=%08lX",
                     static_cast<unsigned long>(ntos_hw::g_context.last_ssr),
                     static_cast<unsigned long>(ntos_hw::g_context.last_mmucr));
    }
    LCD_Refresh();
#endif
    if(api_raw) Mem_Free(api_raw);
    for(unsigned i=0u;i<NTOS_MAX_MODULES;++i) {
        if(nodes[i].mapped.raw) Mem_Free(nodes[i].mapped.raw);
        if(i && nodes[i].file) Mem_Free(nodes[i].file);
    }
    return rc;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
constexpr int kMaxFiles = 64; // 12 stress DLLs plus runtime DLLs and EXEs
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

// A pressed EXE used to acknowledge a load error must be released before
// the picker is redrawn, or the next scan immediately relaunches that EXE.
static void wait_error_key_release(void) {
    // The picker uses the level-based getKey API. Debug_WaitKey uses a
    // different event path and can block after the picker consumed EXE.
    // Wait for the launch key to go up, then require a NEW press.
    uint32_t key1=0u,key2=0u;
    do {
        getKey(&key1,&key2);
    } while ((key1|key2)!=0u);
    do {
        getKey(&key1,&key2);
    } while ((key1|key2)==0u);
    // Do not let the acknowledging key leak into the next picker.
    do {
        getKey(&key1,&key2);
    } while ((key1|key2)!=0u);
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
    int xamlui_idx = -1;
    {
        int npick = 0;
        for (int i = 0; i < count; i++) {
            const char stem[] = "COREDLL";
            int k = 0;
            while (stem[k] != '\0' && names[i][k] != '\0' &&
                   (names[i][k] | 0x20) == (stem[k] | 0x20))
                k++;
            if (stem[k] == '\0' && names[i][k] == '.') {
                coredll_idx = i;
                continue;
            }
            const char xaml_stem[] = "XAMLUI";
            int x=0;
            while (xaml_stem[x]!='\0' && names[i][x]!='\0' &&
                   (names[i][x] | 0x20)==(xaml_stem[x] | 0x20))
                ++x;
            if (xaml_stem[x]=='\0' && names[i][x]=='.') {
                xamlui_idx=i;
                continue;
            }
            // Only actual .EXE files go in the launcher menu.
            int n=0;while (names[i][n]!='\0') ++n;
            if (n>=4 && (names[i][n-4]|0x20)=='.' &&
                (names[i][n-3]|0x20)=='e' &&
                (names[i][n-2]|0x20)=='x' &&
                (names[i][n-1]|0x20)=='e')
                pick[npick++] = i;
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
        Debug_Printf(0, static_cast<unsigned int>(3 + i), false, 0,
                     "%s", line);
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
        Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                     "Any key: return to EXE list");
        LCD_Refresh();
        wait_error_key_release();
        continue;
    }
    uint32_t fsize = sizes[pick[selected]];
    auto *file = static_cast<uint8_t *>(Mem_Malloc(fsize != 0 ? fsize : 1));
    if (file == nullptr) {
        ntos_puts("OOM reading file");
        File_Close(fd);
        Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                     "Any key: return to EXE list");
        LCD_Refresh();
        wait_error_key_release();
        continue;
    }
    if (File_Read(fd, file, static_cast<int>(fsize)) !=
        static_cast<int>(fsize)) {
        ntos_puts("File_Read failed");
        Mem_Free(file);
        File_Close(fd);
        Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                     "Any key: return to EXE list");
        LCD_Refresh();
        wait_error_key_release();
        continue;
    }
    File_Close(fd);

    // Parse the header once to pick the load path.
    pe::Image hdr{};
    if (pe::parse(file, fsize, &hdr) != 0) {
        ntos_puts("Bad PE header (not SH4BE 0x01A8?)");
        Mem_Free(file);
        Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                     "Any key: return to EXE list");
        LCD_Refresh();
        wait_error_key_release();
        continue;
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
        const char *extra_path=nullptr;
        uint32_t extra_size=0;
        char extra_path_buf[8+kMaxName+1];
        if (xamlui_idx>=0) {
            const char prefix[] = "\\fls0\\";
            int p=0;
            for (;prefix[p];++p) extra_path_buf[p]=prefix[p];
            for (int j=0;names[xamlui_idx][j];++j)
                extra_path_buf[p++]=names[xamlui_idx][j];
            extra_path_buf[p]=0;
            extra_path=extra_path_buf;
            extra_size=sizes[xamlui_idx];
        }
        ntos_puts("Mode: UTLB-mapped (preferred base)");
        const char *err = nullptr;
        if (run_mapped(file,fsize,hdr,core_path,core_size,
                       extra_path,extra_size,&ret,&err) != 0) {
            Debug_Printf(0, static_cast<unsigned int>(cur_row++), false, 0,
                         "Mapped load failed: %s", err);
            LCD_Refresh();
            Mem_Free(file);
            Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                     "Any key: return to EXE list");
        LCD_Refresh();
        wait_error_key_release();
        continue;
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
            Debug_Printf(0,static_cast<unsigned int>(cur_row++),false,0,
                     "Any key: return to EXE list");
        LCD_Refresh();
        wait_error_key_release();
        continue;
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
    // Debug_WaitKey accepts arbitrary keys, while the prompt promises that
    // only EXE reopens the picker and CLEAR exits.  Handle both explicitly.
    // Wait for the launch key to be released so a held EXE cannot relaunch.
    {
        uint32_t key1 = 0, key2 = 0;
        do {
            getKey(&key1, &key2);
        } while ((key1 | key2) != 0);
        for (;;) {
            getKey(&key1, &key2);
            if (testKey(key1, key2, KEY_CLEAR))
                return 0;
            if (testKey(key1, key2, KEY_EXE))
                break;
        }
        // Consume EXE release before repopulating the selection screen.
        do {
            getKey(&key1, &key2);
        } while ((key1 | key2) != 0);
    }
    } // relaunch loop: back to the banner + picker
}
