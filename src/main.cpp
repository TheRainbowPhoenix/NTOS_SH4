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
#include <new>
#include <sdk/calc/calc.h>
#include <sdk/os/debug.h>
#include <sdk/os/file.h>
#include <sdk/os/lcd.h>
#include <sdk/os/input.h>
#include <sdk/os/mem.h>

#include "../payload/ntapi_caps.h"
#include "pe.hpp"
#include "console.hpp"
#include "console_font.hpp"
#include "pe_rebase.hpp"
#include "pe_pager.hpp"
#include "pe_stream.hpp"
#include "host_world.hpp"
#include "irq_handoff.hpp"
#include "mmu.hpp"
#include "nt/mm_protect.hpp"
#include "nt/launch_policy.hpp"
#include "nt/picker.hpp"
#include "nt/native_vm.hpp"
#include "nt/guest_buffer.hpp"
#include "fs_guard.hpp"
#include "nt/process.hpp"
#include "nt/ke_coop.hpp"
#include "nt/io_manager.hpp"
#include "hal/sh7305_tmu_snapshot.hpp"
#include "hal/sh7305_usb_power.hpp"
#include "nt/dll_lifecycle.hpp"
#ifndef NTOS_ENABLE_COOP_KERNEL
#define NTOS_ENABLE_COOP_KERNEL 0
#endif
#ifndef NTOS_ENABLE_GUEST_IO
#define NTOS_ENABLE_GUEST_IO 0
#endif
#ifndef NTOS_ENABLE_PE_PAIR
#define NTOS_ENABLE_PE_PAIR 0
#endif
#ifndef NTOS_ENABLE_PSROOT
#define NTOS_ENABLE_PSROOT 0
#endif
#ifndef NTOS_ENABLE_PS_EXEC
#define NTOS_ENABLE_PS_EXEC 0
#endif
#ifndef NTOS_ENABLE_PSUSER
#define NTOS_ENABLE_PSUSER 0
#endif
#ifndef NTOS_ENABLE_CHILD_PS_RESOURCES
#define NTOS_ENABLE_CHILD_PS_RESOURCES 0
#endif
#if NTOS_ENABLE_CHILD_PS_RESOURCES && (!NTOS_ENABLE_PE_PAIR || !NTOS_ENABLE_PSROOT)
#error "child Ps tables require dual Ke/Ps canaries"
#endif
#if NTOS_ENABLE_PSUSER && (!NTOS_ENABLE_KE_USER_SYSCALL || !NTOS_ENABLE_TLB_MISS_PAGER)
#error "PSUSER requires Ke TRAPA and the P0 pager"
#endif
#if NTOS_ENABLE_PS_EXEC && (!NTOS_ENABLE_PSROOT || !NTOS_ENABLE_TLB_MISS_PAGER)
#error "PSCODE requires physical PSROOT and the owned VBR pager"
#endif
#ifndef NTOS_ENABLE_DLL_INIT_TRACE
#define NTOS_ENABLE_DLL_INIT_TRACE 0
#endif
#ifndef NTOS_PE_STREAM_LOG
#define NTOS_PE_STREAM_LOG 1
#endif
#ifndef NTOS_NO_LOADER_PROGRESS_LOG
#define NTOS_NO_LOADER_PROGRESS_LOG 0
#endif

// Streaming is enabled on this development branch. Set 0 to compare the
// previous eager full-file staging path during emulator/hardware testing.
#ifndef NTOS_STREAMED_IMAGE_LOADING
#define NTOS_STREAMED_IMAGE_LOADING 1
#endif

APP_NAME("NTOS")
APP_DESCRIPTION("Big-endian SH4 COFF EXE loader")
APP_AUTHOR("recreate project")
APP_VERSION("0.4")

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
    int32_t (*launch)(const char *);                       // [27] serial app handoff
    int32_t (*create_event)(uint32_t,uint32_t);             // [28] manual, initial
    uint32_t (*set_event)(uint32_t);                        // [29]
    uint32_t (*reset_event)(uint32_t);                      // [30]
    uint32_t (*wait_event)(uint32_t,uint32_t);              // [31] timeout ms; 0 supported
    uint32_t (*close_object)(uint32_t);                     // [32] NTSTATUS
    void *(*ex_alloc)(uint32_t,uint32_t);                   // [33] bytes, tag
    uint32_t (*ex_free)(void *,uint32_t);                   // [34] ptr, tag
    uint32_t (*ex_pool_peak)(void);                         // [35]
    uint32_t (*ex_pool_live)(void);                         // [36]
    uint32_t (*ob_handle_count)(void);                      // [37]
    uint32_t (*mm_page_rights)(uint32_t);                  // [38] Mm RO/RW metadata
    int32_t (*mm_arm_fault_probe)(uint32_t,uint32_t);      // [39] VA, expected EXPEVT
    uint32_t (*mm_fault_probe_seen)(void);                 // [40] VBR canary callback count
    uint32_t (*vm_reserve)(uint32_t,uint32_t);             // [41] preferred VA, pages -> VA or 0
    uint32_t (*vm_commit)(uint32_t,uint32_t,uint32_t);     // [42] VA,pages,1 RO/2 RW -> NTSTATUS
    uint32_t (*vm_protect)(uint32_t,uint32_t,uint32_t);    // [43] live PR update
    uint32_t (*vm_query)(uint32_t);                       // [44] 0 reserved,1 RO,2 RW
    uint32_t (*vm_release)(uint32_t);                     // [45] reserved-only release
    uint32_t (*vm_committed_pages)(void);                 // [46] per-launch tracked frames
    uint32_t (*arm_guest_guard)(uint32_t,uint32_t,uint32_t); // [47] TEA,EV,resume PC
    uint32_t (*guard_seen)(void);                         // [48] accepted violations
    uint32_t (*guard_status)(void);                       // [49] NTSTATUS
    uint32_t (*guard_fault_address)(void);                // [50] TEA
    uint32_t (*nt_allocate_virtual_memory)(
        uint32_t,uint32_t *,uint32_t,uint32_t *,uint32_t,uint32_t); // [51]
    uint32_t (*nt_protect_virtual_memory)(
        uint32_t,uint32_t *,uint32_t *,uint32_t,uint32_t *); // [52]
    uint32_t (*nt_free_virtual_memory)(
        uint32_t,uint32_t *,uint32_t *,uint32_t);         // [53]
    uint32_t (*ke_arm_system_call)(uint32_t,uint32_t);    // [54] verified TRAPA PC, service
    uint32_t (*ps_get_current_process_id)(void);        // [55]
    uint32_t (*ps_get_current_thread_id)(void);         // [56]
    uint32_t (*ke_system_service_count)(void);          // [57]
    uint32_t (*ke_register_exception)(uint32_t,uint32_t,uint32_t,uint32_t); // [58]
    uint32_t (*ke_cancel_exception)(uint32_t);          // [59]
    uint32_t (*ke_exception_delivered)(void);           // [60]
    uint32_t (*ke_exception_status)(void);              // [61]
    uint32_t (*ke_exception_address)(void);             // [62]
    uint32_t (*ke_exception_depth)(void);               // [63]
    uint32_t (*ps_address_space_id)(void);              // [64]
    uint32_t (*ps_address_space_epoch)(void);           // [65]
    uint32_t (*ps_inherited_from_process_id)(void);     // [66]
    uint32_t (*ke_create_coop_thread)(uint32_t,uint32_t); // [67] entry, P0 stack-top -> TID
    uint32_t (*ke_coop_yield)(void);                   // [68]
    uint32_t (*ke_coop_exit)(void);                    // [69] secondary-only
    uint32_t (*ke_coop_switch_count)(void);            // [70]
    uint32_t (*nt_open_device)(const char *,uint32_t *); // [71] NTSTATUS
    uint32_t (*nt_read_device)(uint32_t,void *,uint32_t,uint32_t *); // [72]
    uint32_t (*nt_write_device)(uint32_t,const void *,uint32_t,uint32_t *); // [73]
    uint32_t (*nt_close_device)(uint32_t);              // [74] NTSTATUS
    uint32_t (*nt_device_io_control)(uint32_t,uint32_t,void *,uint32_t,
                                      uint32_t *);      // [75] NTSTATUS
    uint32_t (*ps_peer_entry)(void);                   // [76] second EXE entry
    uint32_t (*ps_peer_tick)(void);                    // [77] secondary progress
    uint32_t (*ps_peer_progress)(void);                // [78] total ticks
    uint32_t (*psroot_va)(void);                      // [79] P0 canary VA
    uint32_t (*psroot_switch)(uint32_t);               // [80] NTSTATUS
    uint32_t (*psroot_current)(void);                  // [81] root 0 or 1
    uint32_t (*pstwin_child_pid)(void);                // [82] child Ps PID
    uint32_t (*io_replace_loop_driver)(uint32_t);      // [83] diagnostic IoReplaceDriver
    uint32_t (*ps_exec_entry)(void);                    // [84] same-VA executable entry
    uint32_t (*ps_user_stack_top)(void);                 // [85] actual P0 user stack
    uint32_t (*ps_user_arm)(uint32_t,uint32_t);          // [86] vetted TRAPA sites
    uint32_t (*ps_user_syscalls)(void);                  // [87] actual VBR dispatches
    uint32_t (*io_open_handle_count)(void);             // [88] kernel Io FILE_OBJECT count
};

static int cur_row = 0;

// The bitmap atlas contains 14 distinct rows per glyph. The old 12-row
// resampling skipped source row 6 (the middle bar in E/F/B); do not squeeze
// rows. 37 lines x 14 pixels fit inside the 320x528 LCD (518 pixels).
constexpr unsigned NTOS_CONSOLE_COLUMNS=ntos_console::COLUMNS;
constexpr unsigned NTOS_CONSOLE_ROWS=ntos_console::ROWS;
constexpr unsigned NTOS_CONSOLE_GLYPH_HEIGHT=ntos_console::GLYPH_HEIGHT;
static TextConsole<NTOS_CONSOLE_COLUMNS,NTOS_CONSOLE_ROWS> app_console;
static bool app_console_started = false;
static void console_paint(unsigned row, const char *text) {
    const unsigned top = row * NTOS_CONSOLE_GLYPH_HEIGHT;
    for (unsigned y = top; y < top + NTOS_CONSOLE_GLYPH_HEIGHT && y < height; ++y)
        line(0, static_cast<int>(y), static_cast<int>(width - 1), static_cast<int>(y), 0xffff);
    for (unsigned x = 0; x < NTOS_CONSOLE_COLUMNS && text[x]; ++x) {
        unsigned ch = static_cast<unsigned char>(text[x]);
        if (ch < 32 || ch > 126) ch = '?';
        for (unsigned y = 0; y < NTOS_CONSOLE_GLYPH_HEIGHT; ++y) {
            const unsigned bits = ntos_console::glyph_row(ch,y);
            for (unsigned bit = 0; bit < 8; ++bit)
                if (bits & (1u << bit)) setPixel(x * 8 + bit, top + y, 0);
        }
    }
}
static bool console_scroll() {
    // Scroll 36 complete 14-pixel lines; the 37th line is repainted
    // by TextConsole::newline. Never shear a glyph mid-row.
    volatile uint16_t *frame = vram;
    for (unsigned i = 0; i < 320u * (NTOS_CONSOLE_ROWS - 1u) * NTOS_CONSOLE_GLYPH_HEIGHT; ++i)
        frame[i] = frame[i + 320u * NTOS_CONSOLE_GLYPH_HEIGHT];
    return true;
}
static void ntos_puts(const char *text) {
    if (!text) return;
    if (static_cast<unsigned>(cur_row) != app_console.row())
        app_console.position(static_cast<unsigned>(cur_row));
    app_console.puts(text, console_paint, console_scroll);
    cur_row = static_cast<int>(app_console.row());
    LCD_Refresh();
}
static void log_text(char *&dst, const char *text) {
    while (*text) *dst++ = *text++;
}
static void log_number(char *&dst, uint32_t value, unsigned base, unsigned width = 0) {
    char digits[32];
    unsigned n = 0;
    do { digits[n++] = "0123456789ABCDEF"[value % base]; value /= base; } while (value);
    while (n < width) digits[n++] = '0';
    while (n) *dst++ = digits[--n];
}
static void ntos_app_puts(const char *text) {
    if (!app_console_started) {
        fillScreen(0xffff);
        app_console.reset(); cur_row = 0; app_console_started = true;
    }
    ntos_puts(text);
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
// All SDK descriptors belong to one serialized NTOS gate. Do not confuse
// a WinCE synthetic COREDLL HANDLE with an SDK descriptor.
static ntos_fs::Gate g_fs;
// This is NOT a protected process address space: it is the owner of one
// foreground application graph, its NT handles, events and Ex allocations.
#ifndef NTOS_PROCESS_HEAP_RESIDENT
#define NTOS_PROCESS_HEAP_RESIDENT 0
#endif
#ifndef NTOS_NATIVE_TMU_SNAPSHOT
#define NTOS_NATIVE_TMU_SNAPSHOT 0
#endif
#ifndef NTOS_NATIVE_USB_SNAPSHOT
#define NTOS_NATIVE_USB_SNAPSHOT 0
#endif
#if NTOS_PROCESS_HEAP_RESIDENT
// The foreground Process is much larger than the 64-KiB HH3 stage0.
// Construct it ONCE in external P1 memory before the first guest and
// preserve its exact address across launches (handles/events hold pointers).
// NEVER allocate or free this object while the NTOS VBR/MMU is installed.
static nt::Process *g_nt_process_storage=nullptr;
#define g_nt_process (*g_nt_process_storage)
#else
static nt::Process g_nt_process{};
#endif
#if NTOS_ENABLE_PE_PAIR
struct PePairState {
    uint32_t entry=0u,base=0u,progress=0u;
    bool ready=false;
};
static PePairState g_pe_pair{};
#endif
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
// A bounded hardware two-root P0 *data-page* canary, independent of VM.
struct PsRootHardware {
    uint32_t va=0u,pa[2]{},parent_pid=0u,switches=0u;
    uint8_t *backing[2]{};
    unsigned module_index=0u,active=0u;
#if NTOS_ENABLE_PS_EXEC
    uint32_t exec_va=0u,exec_entry=0u,exec_pa[2]{};
    uint8_t *exec_backing[2]{};
    unsigned exec_module_index=0u,exec_pages=0u;
#endif
    nt::KPROCESS child{};
    bool ready=false,twin=false;
};
static PsRootHardware g_ps_root{};
#endif
#if NTOS_ENABLE_CHILD_PS_RESOURCES
// Held in external P1 HH3 heap, NOT 64KiB launcher data/BSS. Construct
// before VBR takeover, clear and release only after all child mappings die.
static nt::Process *g_ps_child_process=nullptr;
static nt::Process &ntos_process_current() {
    return g_ps_child_process && g_ps_child_process->active &&
           g_ps_root.twin && g_ps_root.active==1u ?
           *g_ps_child_process : g_nt_process;
}
#else
static nt::Process &ntos_process_current() {
    return g_nt_process;
}
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
struct PsUserStack {
    uint32_t va=0u,physical=0u;
    uint8_t *backing=nullptr;
    bool ready=false;
};
static PsUserStack g_ps_user{};
#endif
#if NTOS_ENABLE_GUEST_IO
struct NtIoBuiltIn {
    nt::io::Manager<3u+NTOS_NATIVE_TMU_SNAPSHOT+
                    NTOS_NATIVE_USB_SNAPSHOT,8u> manager{};
    nt::io::DRIVER_OBJECT null_driver{};
    nt::io::DRIVER_OBJECT loop_driver{};
    nt::io::DRIVER_OBJECT loop_swapped{};
    nt::io::DRIVER_OBJECT display_driver{};
#if NTOS_NATIVE_TMU_SNAPSHOT
    nt::io::DRIVER_OBJECT timer_driver{};
    nt::io::DEVICE_OBJECT *timer_device=nullptr;
#endif
#if NTOS_NATIVE_USB_SNAPSHOT
    nt::io::DRIVER_OBJECT usb_power_driver{};
    nt::io::DEVICE_OBJECT *usb_power_device=nullptr;
#endif
    nt::io::DEVICE_OBJECT *null_device=nullptr,*loop_device=nullptr,
                          *display_device=nullptr;
    uint8_t loop[64]{};
    uint32_t loop_bytes=0u;
    bool enabled=false;
    static nt::Status dispatch(void *opaque,nt::io::DEVICE_OBJECT &dev,
                               nt::io::IRP &irp) {
        auto *s=static_cast<NtIoBuiltIn *>(opaque);
        if(!s || !s->enabled)return nt::DEVICE_BUSY;
        using namespace nt::io;
        nt::Status status=nt::SUCCESS;
        uint32_t transferred=0u;
#if NTOS_NATIVE_USB_SNAPSHOT
        if(&dev==s->usb_power_device) {
            if(irp.major==Major::create || irp.major==Major::close) {
                // USB power state and host OS USB IRQ ownership unchanged.
            } else if(irp.major!=Major::device_control) {
                status=nt::NOT_SUPPORTED;
            } else {
                uint32_t sr=0u;
                __asm__ volatile("stc sr,%0" : "=r"(sr) :: "memory");
                if((sr&0x70000000u)!=0x40000000u)
                    status=nt::DEVICE_BUSY;
                else status=ntos_hal::usb_power_status(
                    irp.buffer,irp.length,irp.control_code,
                    &ntos_hal::usb_power_read_native,nullptr);
                if(status==nt::SUCCESS)
                    transferred=sizeof(ntos_hal::UsbPowerStatus);
            }
            return IoCompleteRequest(irp,status,transferred)==nt::SUCCESS ?
                status : nt::INTERNAL_ERROR;
        }
#endif
#if NTOS_NATIVE_TMU_SNAPSHOT
        if(&dev==s->timer_device) {
            if(irp.major==Major::create || irp.major==Major::close) {
                // No timer state touched on open/close.
            } else if(irp.major!=Major::device_control) {
                status=nt::NOT_SUPPORTED;
            } else {
                uint32_t sr=0u;
                __asm__ volatile("stc sr,%0" : "=r"(sr) :: "memory");
                if((sr&0x70000000u)!=0x40000000u)
                    status=nt::DEVICE_BUSY;
                else status=ntos_hal::tmu_snapshot(
                    irp.buffer,irp.length,irp.control_code,
                    &ntos_hal::tmu_read_native,nullptr);
                if(status==nt::SUCCESS)
                    transferred=sizeof(ntos_hal::TmuSnapshot);
            }
            return IoCompleteRequest(irp,status,transferred)==nt::SUCCESS ?
                status : nt::INTERNAL_ERROR;
        }
#endif
        if(&dev==s->display_device) {
            // First hardware-facing NT driver: use the already proven
            // CP400 framebuffer HAL. Never call GetInput, File_* or
            // manipulate VBR/IRQ state from this normal Ke service.
            if(irp.major==Major::create || irp.major==Major::close) {
                // Nothing to acquire; ownership stays in the HH3 HAL.
            } else if(irp.major==Major::device_control &&
                      irp.control_code==0x4E540001u &&
                      irp.length==12u && irp.buffer) {
                const auto *v=static_cast<const uint8_t *>(irp.buffer);
                auto rd=[&](unsigned i)->uint32_t {
                    return (uint32_t(v[i])<<24)|
                           (uint32_t(v[i+1u])<<16)|
                           (uint32_t(v[i+2u])<<8)|v[i+3u];
                };
                const uint32_t x=rd(0u),y=rd(4u),color=rd(8u);
                if(x>=static_cast<uint32_t>(width) ||
                   y>=static_cast<uint32_t>(height) ||
                   color>0xFFFFu)status=nt::INVALID_PARAMETER;
                else {
                    ntos_pixel(x,y,color);
                    transferred=12u;
                }
            } else status=nt::NOT_SUPPORTED;
            return IoCompleteRequest(irp,status,transferred)==nt::SUCCESS ?
                status : nt::INTERNAL_ERROR;
        }
        const bool is_loop=&dev==s->loop_device;
        switch(irp.major) {
        case Major::create:case Major::close: break;
        case Major::write:
            transferred=irp.length;
            if(is_loop) {
                if(irp.length>sizeof(s->loop))return nt::INVALID_PARAMETER;
                const auto *src=static_cast<const uint8_t *>(irp.buffer);
                for(uint32_t i=0u;i<irp.length;++i)s->loop[i]=src[i];
                s->loop_bytes=irp.length;
            }
            break;
        case Major::read:
            if(is_loop) {
                transferred=irp.length<s->loop_bytes?irp.length:s->loop_bytes;
                auto *dst=static_cast<uint8_t *>(irp.buffer);
                for(uint32_t i=0u;i<transferred;++i)
                    dst[i]=dev.driver==&s->loop_swapped ?
                           static_cast<uint8_t>(s->loop[i]^0xFFu) :
                           s->loop[i];
            }
            break;
        case Major::device_control:
            status=nt::NOT_SUPPORTED;break;
        default:status=nt::INVALID_PARAMETER;break;
        }
        return nt::io::IoCompleteRequest(irp,status,transferred)==nt::SUCCESS ?
               status : nt::INTERNAL_ERROR;
    }
    bool reset() {
        *this=NtIoBuiltIn{};
        null_driver={&dispatch,this};
        loop_driver={&dispatch,this};
        loop_swapped={&dispatch,this};
        display_driver={&dispatch,this};
#if NTOS_NATIVE_TMU_SNAPSHOT
        timer_driver={&dispatch,this};
#endif
#if NTOS_NATIVE_USB_SNAPSHOT
        usb_power_driver={&dispatch,this};
#endif
        enabled=true;
        return manager.IoCreateDevice("\\Device\\Null",&null_driver,
                          &null_device)==nt::SUCCESS &&
               manager.IoCreateDevice("\\Device\\Loopback",&loop_driver,
                          &loop_device)==nt::SUCCESS &&
               manager.IoCreateDevice("\\Device\\Display",&display_driver,
                          &display_device)==nt::SUCCESS
#if NTOS_NATIVE_TMU_SNAPSHOT
            && manager.IoCreateDevice("\\Device\\Timer",&timer_driver,
                          &timer_device)==nt::SUCCESS
#endif
#if NTOS_NATIVE_USB_SNAPSHOT
            && manager.IoCreateDevice("\\Device\\UsbStatus",
                          &usb_power_driver,&usb_power_device)==nt::SUCCESS
#endif
            ;
    }
    nt::Status replace_loop(uint32_t variant) {
        if(!enabled || variant>1u || !loop_device)
            return nt::INVALID_PARAMETER;
        return manager.IoReplaceDriver(
            *loop_device,variant?&loop_swapped:&loop_driver);
    }
    void shutdown(uint32_t pid) {
        if(enabled && pid)(void)manager.close_owner(pid);
        enabled=false; // no P0 dereferences or SDK access during teardown
    }
};
static NtIoBuiltIn g_nt_io{};
#endif
#if NTOS_ENABLE_COOP_KERNEL
static nt::KeCoop g_ke_coop{};
extern "C" void ntos_ke_coop_swap(nt::CoopSaved *,const nt::CoopSaved *);
#endif
static void *nt_host_alloc(void *,size_t n) {return Mem_Malloc(n);}
static void nt_host_free(void *,void *p) {Mem_Free(p);}
static bool nt_process_configure() {
#if NTOS_PROCESS_HEAP_RESIDENT
    if(!g_nt_process_storage) {
        void *raw=Mem_Malloc(sizeof(nt::Process));
        if(!raw)return false;
        g_nt_process_storage=::new(raw) nt::Process{};
    }
#endif
    g_nt_process.pool.configure(nullptr,&nt_host_alloc,&nt_host_free);
#if NTOS_ENABLE_GUEST_IO
    (void)g_nt_io.reset();
#endif
#if NTOS_ENABLE_COOP_KERNEL
    // Only called before the next mapped guest. Never reset live stacks.
    g_ke_coop.reset();
#endif
    return true;
}
struct FatalSnapshot {
    uint32_t tea=0,expevt=0,spc=0,ssr=0,mmucr=0,reason=0;
};
static FatalSnapshot g_fatal;

static int sdk_close_file(int h) { return File_Close(h); }
static int sdk_close_find(int h) { return File_FindClose(h); }
static bool fs_recover() { return g_fs.cleanup(sdk_close_file,sdk_close_find); }
static int fs_open(const char *path,int flags) {
    if(g_fs.busy())return -16;  // busy: never issue a second SDK open
    int fd=File_Open(path,flags);
    if(fd>=0 && !g_fs.acquire_file(fd)) {
        (void)File_Close(fd);
        return -16;
    }
    return fd;
}
static int fs_close(int fd) {
    if(!g_fs.file_is(fd))return -2;
    return g_fs.close(sdk_close_file,sdk_close_find) ? 0 : -1;
}
static int fs_find_first(const char_const16_t *pattern,int *handle,
                         char_const16_t *name,File_FindInfo *info) {
    if(g_fs.busy())return -16;
    int found=-1;
    int rc=File_FindFirst(pattern,&found,name,info);
    if(rc==0) {
        if(!g_fs.acquire_finder(found)) {
            (void)File_FindClose(found);
            return -16;
        }
        *handle=found;
    }
    return rc;
}
static int fs_find_next(int handle,char_const16_t *name,File_FindInfo *info) {
    if(!g_fs.finder_is(handle))return -2;
    return File_FindNext(handle,name,info);
}
static int fs_find_close(int handle) {
    if(!g_fs.finder_is(handle))return -2;
    return g_fs.close(sdk_close_file,sdk_close_find) ? 0 : -1;
}
static int32_t ntos_file_open(const char *name, uint32_t flags) {
    char path[96];
    if (!kernel_path(path,name)) return -2;
    if(!g_nt_process.active || g_fs.busy())return -16;
    const int sdk_fd=fs_open(path,static_cast<int>(flags));
    if(sdk_fd<0)return sdk_fd;
    uint32_t token=0u;
    if(g_nt_process.attach_file(sdk_fd,&token)!=nt::SUCCESS) {
        // The SDK file was positively opened. Close exactly once, and
        // quarantine every subsequent operation if the close is ambiguous.
        return fs_close(sdk_fd)==0 ? -12 : -1;
    }
    return static_cast<int32_t>(token);
}
static int32_t ntos_file_read(int32_t fd, void *data, uint32_t count) {
    const int32_t sdk_fd=g_nt_process.file_fd(static_cast<uint32_t>(fd));
    if (!data || count>4096u || !g_fs.file_is(sdk_fd)) return -2;
    auto *dst=static_cast<uint8_t *>(data);
    uint8_t bounce[256];
    uint32_t total=0;
    while (total<count) {
        uint32_t amount=count-total;
        if (amount>sizeof(bounce)) amount=sizeof(bounce);
        int n=File_Read(sdk_fd,bounce,static_cast<int>(amount));
        if (n<0) return total ? static_cast<int32_t>(total) : n;
        if(static_cast<uint32_t>(n)>amount)return -2; // bad SDK byte count
        for (int i=0;i<n;++i) dst[total+static_cast<uint32_t>(i)]=bounce[i];
        total+=static_cast<uint32_t>(n);
        if (n<static_cast<int>(amount)) break;
    }
    return static_cast<int32_t>(total);
}
static int32_t ntos_file_write(int32_t fd,const void *data,uint32_t count) {
    const int32_t sdk_fd=g_nt_process.file_fd(static_cast<uint32_t>(fd));
    if (!data || count>4096u || !g_fs.file_is(sdk_fd)) return -2;
    const auto *src=static_cast<const uint8_t *>(data);
    uint8_t bounce[256];
    uint32_t total=0;
    while (total<count) {
        uint32_t amount=count-total;
        if (amount>sizeof(bounce)) amount=sizeof(bounce);
        for (uint32_t i=0;i<amount;++i) bounce[i]=src[total+i];
        int n=File_Write(sdk_fd,bounce,static_cast<int>(amount));
        if(n<0) return total ? static_cast<int32_t>(total) : n;
        if(n==0 || static_cast<uint32_t>(n)>amount)
            return total ? static_cast<int32_t>(total) : -5;
        total+=static_cast<uint32_t>(n);
        if(static_cast<uint32_t>(n)<amount)break; // short write: no blind retry
        if (n<static_cast<int>(amount)) break;
    }
    return static_cast<int32_t>(total);
}
static int32_t ntos_file_close(int32_t fd) {
    const uint32_t token=static_cast<uint32_t>(fd);
    const int32_t sdk_fd=g_nt_process.file_fd(token);
    if(!g_fs.file_is(sdk_fd))return -2;
    // The SDK close MUST happen before discarding the public handle.
    // On ambiguous failure g_fs becomes poisoned: no retry, no reuse.
    if(fs_close(sdk_fd)!=0)return -1;
    return g_nt_process.drop_file(token)==nt::SUCCESS ? 0 : -2;
}

// Structured input: kind 1=touch down/drag, 2=CLEAR, 3=key press.
// On key press x contains the Hollyhock KEYCODE_* value.
static uint32_t ntos_read_event(uint32_t *x, uint32_t *y,
                                 uint32_t *direction) {
    Input_Event event{};
    GetInput(&event, 0xFFFFFFFFu, 0x10);
    if (event.type == EVENT_TOUCH) {
        if (event.data.touch_single.direction != TOUCH_DOWN &&
            event.data.touch_single.direction != TOUCH_HOLD_DRAG &&
            event.data.touch_single.direction != TOUCH_UP &&
            event.data.touch_single.direction != TOUCH_ACT_BAR) return 0;
        if (x) *x = static_cast<uint32_t>(event.data.touch_single.p1_x);
        if (y) *y = static_cast<uint32_t>(event.data.touch_single.p1_y);
        if (direction) *direction = event.data.touch_single.direction==TOUCH_ACT_BAR?1u:static_cast<uint32_t>(event.data.touch_single.direction);
        return 1;
    }
    if(event.type==EVENT_ACTBAR_ESC)return 2;
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
    const int32_t sdk_fd=g_nt_process.file_fd(static_cast<uint32_t>(fd));
    if (!g_fs.file_is(sdk_fd) || File_Fstat(sdk_fd, &st) < 0) return -1;
    return static_cast<int32_t>(st.fileSize);
}

static int32_t ntos_file_seek(int32_t fd,int32_t offset,uint32_t whence) {
    if (whence>2) return -2;
    const int32_t sdk_fd=g_nt_process.file_fd(static_cast<uint32_t>(fd));
    if (!g_fs.file_is(sdk_fd)) return -2;
    return File_Lseek(sdk_fd,offset,static_cast<File_Whence>(whence));
}
static int32_t ntos_file_remove(const char *name) {
    char path[96];
    if (!kernel_path(path,name)) return -2;
    if(g_fs.busy())return -16;
    return File_Remove(path);
}
static int32_t ntos_file_rename(const char *from,const char *to) {
    char source[96],dest[96];
    if (!kernel_path(source,from) || !kernel_path(dest,to)) return -2;
    if(g_fs.busy())return -16;
    return File_Rename(source,dest);
}
static int32_t ntos_file_mkdir(const char *name) {
    char path[96];
    if (!kernel_path(path,name)) return -2;
    if(g_fs.busy())return -16;
    return File_Mkdir(path);
}
static int32_t ntos_file_stat(const char *name) {
    char path[96];
    if (!kernel_path(path,name)) return -2;
    struct File_Stat st{};
    if(g_fs.busy())return -16;
    return File_Stat(path,&st);
}

static uint32_t ntos_getkey(void) {
    uint32_t key1 = 0;
    uint32_t key2 = 0;
    getKey(&key1, &key2);
    return (key1 << 8) | (key2 & 0xFFu);
}

static int32_t ntos_page_hint(uint32_t va);
static uint32_t ntos_mm_page_rights(uint32_t va);
static int32_t ntos_mm_arm_fault_probe(uint32_t va,uint32_t event);
static uint32_t ntos_mm_fault_probe_seen(void);
static uint32_t ntos_vm_reserve(uint32_t base,uint32_t pages);
static uint32_t ntos_vm_commit(uint32_t base,uint32_t pages,uint32_t rights);
static uint32_t ntos_vm_protect(uint32_t base,uint32_t pages,uint32_t rights);
static uint32_t ntos_vm_query(uint32_t va);
static uint32_t ntos_vm_release(uint32_t va);
static uint32_t ntos_vm_committed_pages(void);
static uint32_t ntos_arm_guest_guard(uint32_t,uint32_t,uint32_t);
static uint32_t ntos_guard_seen(void);
static uint32_t ntos_guard_status(void);
static uint32_t ntos_guard_fault_address(void);
static uint32_t ntos_native_allocate(uint32_t,uint32_t *,uint32_t,
                                     uint32_t *,uint32_t,uint32_t);
static uint32_t ntos_native_protect(uint32_t,uint32_t *,uint32_t *,
                                    uint32_t,uint32_t *);
static uint32_t ntos_native_free(uint32_t,uint32_t *,uint32_t *,uint32_t);
static uint32_t ntos_ke_arm_system_call(uint32_t,uint32_t);
static uint32_t ntos_ps_get_current_process_id(void);
static uint32_t ntos_ps_get_current_thread_id(void);
static uint32_t ntos_ke_system_service_count(void);
static uint32_t ntos_ke_register_exception(uint32_t,uint32_t,uint32_t,uint32_t);
static uint32_t ntos_ke_cancel_exception(uint32_t);
static uint32_t ntos_ke_exception_delivered(void);
static uint32_t ntos_ke_exception_status(void);
static uint32_t ntos_ke_exception_address(void);
static uint32_t ntos_ke_exception_depth(void);
static uint32_t ntos_ps_address_space_id(void);
static uint32_t ntos_ps_address_space_epoch(void);
static uint32_t ntos_ps_inherited_from_process_id(void);
static uint32_t ntos_ke_create_coop_thread(uint32_t,uint32_t);
static uint32_t ntos_ke_coop_yield(void);
static uint32_t ntos_ke_coop_exit(void);
static uint32_t ntos_ke_coop_switch_count(void);
static uint32_t ntos_nt_open_device(const char *,uint32_t *);
static uint32_t ntos_nt_read_device(uint32_t,void *,uint32_t,uint32_t *);
static uint32_t ntos_nt_write_device(uint32_t,const void *,uint32_t,uint32_t *);
static uint32_t ntos_nt_close_device(uint32_t);
static uint32_t ntos_nt_device_io_control(uint32_t,uint32_t,void *,uint32_t,
                                           uint32_t *);
static uint32_t ntos_ps_peer_entry(void);
static uint32_t ntos_ps_peer_tick(void);
static uint32_t ntos_ps_peer_progress(void);
static uint32_t ntos_psroot_va(void);
static uint32_t ntos_psroot_switch(uint32_t);
static uint32_t ntos_psroot_current(void);
static uint32_t ntos_pstwin_child_pid(void);
static uint32_t ntos_io_replace_loop_driver(uint32_t);
static uint32_t ntos_ps_exec_entry(void);
static uint32_t ntos_ps_user_stack_top(void);
static uint32_t ntos_ps_user_arm(uint32_t,uint32_t);
static uint32_t ntos_ps_user_syscalls(void);
static uint32_t ntos_io_open_handle_count(void);

// Directory results are bounced through P1 just like file reads.
static void copy_find(NtosFind *out,const char_const16_t *name,const File_FindInfo &info) {
    out->attr=info.type==File_FindInfo::EntryTypeFile ? 0x20u : 0x10u;
    out->size=info.size;
    unsigned i=0;for(;i<127u && name[i];++i)out->name[i]=name[i];out->name[i]=0;
}
static int32_t ntos_find_first(const char *name,int32_t *handle,NtosFind *out) {
    if(!name || !handle || !out || !g_nt_process.active)return -2;
    char path[96];if(!kernel_path(path,name))return -2;
    char_const16_t pattern[96]{},found[260]{};
    for(unsigned i=0;path[i];++i)pattern[i]=static_cast<unsigned char>(path[i]);
    File_FindInfo info{};int search=0;
    int result=fs_find_first(pattern,&search,found,&info);
    if(result!=0)return result;
    uint32_t token=0u;
    if(g_nt_process.attach_finder(search,&token)!=nt::SUCCESS)
        return fs_find_close(search)==0 ? -12 : -1;
    *handle=static_cast<int32_t>(token);
    copy_find(out,found,info);
    return 0;
}
static int32_t ntos_find_next(int32_t handle,NtosFind *out) {
    if(!out)return -2;
    const int sdk_fd=g_nt_process.finder_fd(static_cast<uint32_t>(handle));
    if(!g_fs.finder_is(sdk_fd))return -2;
    char_const16_t found[260]{};File_FindInfo info{};
    const int result=fs_find_next(sdk_fd,found,&info);
    if(result==0)copy_find(out,found,info);
    return result;
}
static int32_t ntos_find_close(int32_t handle) {
    const uint32_t token=static_cast<uint32_t>(handle);
    const int sdk_fd=g_nt_process.finder_fd(token);
    if(!g_fs.finder_is(sdk_fd))return -2;
    if(fs_find_close(sdk_fd)!=0)return -1;
    return g_nt_process.drop_finder(token)==nt::SUCCESS ? 0 : -2;
}

// NT Executive additions. NTSTATUS values are returned unmodified.
static int32_t ntos_create_event(uint32_t manual,uint32_t initial) {
    uint32_t token=0u;
    const nt::Status s=ntos_process_current().create_event(
        manual!=0u,initial!=0u,&token);
    return s==nt::SUCCESS?static_cast<int32_t>(token):static_cast<int32_t>(s);
}
static uint32_t ntos_set_event(uint32_t token) {
    return ntos_process_current().event_set(token);
}
static uint32_t ntos_reset_event(uint32_t token) {
    return ntos_process_current().event_reset(token);
}
static uint32_t ntos_wait_event(uint32_t token,uint32_t ms) {
    return ntos_process_current().event_poll(token,ms);
}
static uint32_t ntos_close_object(uint32_t token) {
    auto &owner=ntos_process_current();
    nt::Object *o=nullptr;
    const nt::Status s=owner.handles.get(token,nt::Type::none,&o);
    if(s!=nt::SUCCESS)return s;
    if(o->type==nt::Type::file)
        return &owner==&g_nt_process &&
               ntos_file_close(static_cast<int32_t>(token))==0 ?
            nt::SUCCESS : nt::DEVICE_BUSY;
    if(o->type==nt::Type::finder)
        return &owner==&g_nt_process &&
               ntos_find_close(static_cast<int32_t>(token))==0 ?
            nt::SUCCESS : nt::DEVICE_BUSY;
    return owner.handles.close(token);
}
static void *ntos_ex_alloc(uint32_t bytes,uint32_t allocation_tag) {
    auto &owner=ntos_process_current();
    return owner.active ?
        owner.pool.alloc(bytes,allocation_tag) : nullptr;
}
static uint32_t ntos_ex_free(void *ptr,uint32_t allocation_tag) {
    auto &owner=ntos_process_current();
    return owner.active ?
        owner.pool.free(ptr,allocation_tag) : nt::DEVICE_BUSY;
}
static uint32_t ntos_ex_pool_peak() {return ntos_process_current().pool.peak;}
static uint32_t ntos_ex_pool_live() {return ntos_process_current().pool.current;}
static uint32_t ntos_ob_handle_count() {return ntos_process_current().handles.used;}

static char pending_app[96];
static uint32_t pending_parent_pid=0u;
static int32_t ntos_launch(const char *name) {
    char path[96];if(!kernel_path(path,name))return 0;
    int fd=fs_open(path,FILE_OPEN_READ);if(fd<0)return 0;
    if(fs_close(fd)<0)return 0;
    unsigned i=0;for(;i+1<sizeof(pending_app)&&path[i];++i)pending_app[i]=path[i];
    pending_app[i]=0;
    pending_parent_pid=g_nt_process.ke.process.active ?
        g_nt_process.ke.process.unique_process_id : 0u;
    return 1;
}
static NtosApi g_ntos_api = {
    &ntos_app_puts,
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
    &ntos_launch,        // [27]
    &ntos_create_event,  // [28]
    &ntos_set_event,     // [29]
    &ntos_reset_event,   // [30]
    &ntos_wait_event,    // [31]
    &ntos_close_object,  // [32]
    &ntos_ex_alloc,      // [33]
    &ntos_ex_free,       // [34]
    &ntos_ex_pool_peak,  // [35]
    &ntos_ex_pool_live,  // [36]
    &ntos_ob_handle_count, // [37]
    &ntos_mm_page_rights, // [38]
    &ntos_mm_arm_fault_probe, // [39]
    &ntos_mm_fault_probe_seen, // [40]
    &ntos_vm_reserve, // [41]
    &ntos_vm_commit, // [42]
    &ntos_vm_protect, // [43]
    &ntos_vm_query, // [44]
    &ntos_vm_release, // [45]
    &ntos_vm_committed_pages, // [46]
    &ntos_arm_guest_guard, // [47]
    &ntos_guard_seen, // [48]
    &ntos_guard_status, // [49]
    &ntos_guard_fault_address, // [50]
    &ntos_native_allocate, // [51]
    &ntos_native_protect, // [52]
    &ntos_native_free, // [53]
    &ntos_ke_arm_system_call, // [54]
    &ntos_ps_get_current_process_id, // [55]
    &ntos_ps_get_current_thread_id, // [56]
    &ntos_ke_system_service_count, // [57]
    &ntos_ke_register_exception, // [58]
    &ntos_ke_cancel_exception, // [59]
    &ntos_ke_exception_delivered, // [60]
    &ntos_ke_exception_status, // [61]
    &ntos_ke_exception_address, // [62]
    &ntos_ke_exception_depth, // [63]
    &ntos_ps_address_space_id, // [64]
    &ntos_ps_address_space_epoch, // [65]
    &ntos_ps_inherited_from_process_id, // [66]
    &ntos_ke_create_coop_thread, // [67]
    &ntos_ke_coop_yield, // [68]
    &ntos_ke_coop_exit, // [69]
    &ntos_ke_coop_switch_count, // [70]
    &ntos_nt_open_device, // [71]
    &ntos_nt_read_device, // [72]
    &ntos_nt_write_device, // [73]
    &ntos_nt_close_device, // [74]
    &ntos_nt_device_io_control, // [75]
    &ntos_ps_peer_entry, // [76]
    &ntos_ps_peer_tick, // [77]
    &ntos_ps_peer_progress, // [78]
    &ntos_psroot_va, // [79]
    &ntos_psroot_switch, // [80]
    &ntos_psroot_current, // [81]
    &ntos_pstwin_child_pid, // [82]
    &ntos_io_replace_loop_driver, // [83]
    &ntos_ps_exec_entry, // [84]
    &ntos_ps_user_stack_top, // [85]
    &ntos_ps_user_arm, // [86]
    &ntos_ps_user_syscalls, // [87]
    &ntos_io_open_handle_count, // [88]
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
#ifndef NTOS_ENABLE_NT_PRIVATE_VM
#define NTOS_ENABLE_NT_PRIVATE_VM 0
#endif
#if NTOS_ENABLE_NT_PRIVATE_VM && !NTOS_ENABLE_TLB_MISS_PAGER
#error "The private NT VM requires the RAM-backed SH4A TLB pager"
#endif

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
static uint32_t ntos_mm_page_rights(uint32_t va) {
#if NTOS_ENABLE_TLB_MISS_PAGER
    auto &ctx=ntos_hw::g_context;
    if(!ctx.installed || !mmu::at_enabled())return 0u;
    return ntos_hw::protection(va);
#else
    (void)va;
    return 0u;
#endif
}
static int32_t ntos_mm_arm_fault_probe(uint32_t va,uint32_t event) {
#if NTOS_ENABLE_TLB_MISS_PAGER
    return ntos_hw::arm_probe(va,event);
#else
    (void)va;(void)event;
    return -1;
#endif
}
static uint32_t ntos_mm_fault_probe_seen(void) {
#if NTOS_ENABLE_TLB_MISS_PAGER
    return ntos_hw::g_context.probe_seen;
#else
    return 0u;
#endif
}
[[maybe_unused]] static bool ntos_vm_busy() {
#if NTOS_ENABLE_NT_PRIVATE_VM
    return !g_nt_process.active || !g_nt_process.vm.backing ||
           !ntos_hw::g_context.installed || !mmu::at_enabled();
#else
    return true;
#endif
}
[[maybe_unused]] static bool ntos_vm_collides(void *opaque,uint32_t va,uint32_t pages) {
#if NTOS_ENABLE_NT_PRIVATE_VM
    const auto &table=*static_cast<const ntos_fault::Pager *>(opaque);
    for(unsigned i=0u;i<table.modules_used;++i)
        if(ntos_rebase::overlap(va,pages,table.modules[i].va,
                                 table.modules[i].pages))
            return true;
#else
    (void)opaque;(void)va;(void)pages;
#endif
    return false;
}
static uint32_t ntos_vm_reserve(uint32_t preferred,uint32_t pages) {
#if NTOS_ENABLE_NT_PRIVATE_VM
    if(ntos_vm_busy())return 0u;
    uint32_t result=0u;
    const auto &table=ntos_hw::g_context.table;
    const nt::Status status=g_nt_process.vm.reserve(
        preferred,pages,&result,&ntos_vm_collides,
        const_cast<ntos_fault::Pager *>(&table));
    return status==nt::SUCCESS?result:0u;
#else
    (void)preferred;(void)pages;
    return 0u;
#endif
}
static uint32_t ntos_vm_commit(uint32_t va,uint32_t pages,uint32_t prot) {
#if NTOS_ENABLE_NT_PRIVATE_VM
    if(ntos_vm_busy())return nt::DEVICE_BUSY;
    if(prot!=1u && prot!=2u &&
       !(NTOS_ENABLE_GUEST_PROTECT_DISPATCH && prot==3u))
        return nt::INVALID_PARAMETER;
    auto &vm=g_nt_process.vm;
    const nt::Status status=vm.commit(va,pages,static_cast<nt::VmAccess>(prot));
    if(status!=nt::SUCCESS)return status;
    for(uint32_t i=0u;i<pages;++i) {
        const int index=vm.find(va+i*0x1000u);
        if(index<0)return nt::INTERNAL_ERROR;
        uint8_t *const p1=vm.backing+
            static_cast<uint32_t>(vm.pages[index].frame)*0x1000u;
        // Publish freshly zeroed P1 frames before a guest UTLB mapping.
        flush_image(p1,0x1000u);
    }
    return nt::SUCCESS;
#else
    (void)va;(void)pages;(void)prot;
    return nt::NOT_SUPPORTED;
#endif
}
[[maybe_unused]] static bool ntos_vm_update_ptel(void *,uint32_t va,
                                ntos_mm::Access rights) {
#if NTOS_ENABLE_NT_PRIVATE_VM
    return ntos_hw::g_context.table.reprotect(
        va,rights,&ntos_hw::commit_translation,nullptr);
#else
    (void)va;(void)rights;return false;
#endif
}
static uint32_t ntos_vm_protect(uint32_t va,uint32_t pages,uint32_t prot) {
#if NTOS_ENABLE_NT_PRIVATE_VM
    if(ntos_vm_busy())return nt::DEVICE_BUSY;
    if(prot!=1u && prot!=2u &&
       !(NTOS_ENABLE_GUEST_PROTECT_DISPATCH && prot==3u))
        return nt::INVALID_PARAMETER;
    const nt::Status status=g_nt_process.vm.protect(
        va,pages,static_cast<nt::VmAccess>(prot),
        &ntos_vm_update_ptel,nullptr);
    if(status==nt::INTERNAL_ERROR)g_fatal.reason=3u; // failed rollback
    return status;
#else
    (void)va;(void)pages;(void)prot;
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_vm_query(uint32_t va) {
#if NTOS_ENABLE_NT_PRIVATE_VM
    if(ntos_vm_busy())return 0u;
    return static_cast<uint32_t>(g_nt_process.vm.query(va));
#else
    (void)va;return 0u;
#endif
}
static uint32_t ntos_vm_release(uint32_t va) {
#if NTOS_ENABLE_NT_PRIVATE_VM
    if(ntos_vm_busy())return nt::DEVICE_BUSY;
    return g_nt_process.vm.release(va);
#else
    (void)va;return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_vm_committed_pages(void) {
#if NTOS_ENABLE_NT_PRIVATE_VM
    return !ntos_vm_busy() ? g_nt_process.vm.committed_pages : 0u;
#else
    return 0u;
#endif
}


static uint32_t ntos_arm_guest_guard(uint32_t address,uint32_t event,
                                      uint32_t recovery_pc) {
#if NTOS_ENABLE_TLB_MISS_PAGER
    if(!g_nt_process.active)return nt::DEVICE_BUSY;
    return ntos_hw::arm_guest_guard(address,event,recovery_pc);
#else
    (void)address;(void)event;(void)recovery_pc;
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_guard_seen(void) {
#if NTOS_ENABLE_TLB_MISS_PAGER
    return ntos_hw::g_context.guest_guard.accepted;
#else
    return 0u;
#endif
}
static uint32_t ntos_guard_status(void) {
#if NTOS_ENABLE_TLB_MISS_PAGER
    return ntos_hw::g_context.guest_guard.last_status;
#else
    return 0u;
#endif
}
static uint32_t ntos_guard_fault_address(void) {
#if NTOS_ENABLE_TLB_MISS_PAGER
    return ntos_hw::g_context.guest_guard.last_tea;
#else
    return 0u;
#endif
}

// NT-style process-local native VM service. Guest in/out pointers are
// deliberately limited to *resident writable P0 pages* owned by the
// current PE/VM. Never dereference an untrusted P1/host/SDK address here.
// These syscalls never open files or allocate in the VBR ISR.
#if NTOS_ENABLE_NT_PRIVATE_VM && NTOS_ENABLE_TLB_MISS_PAGER
static bool native_guest_word(uint32_t *word) {
    if(ntos_vm_busy())return false;
    auto &table=ntos_hw::g_context.table;
    return nt::guest_writable_word(
        reinterpret_cast<uintptr_t>(word),
        [](void *context,uint32_t va) {
            const auto &pager=
                *static_cast<const ntos_fault::Pager *>(context);
            return pager.lookup(va)>=0 &&
                   pager.access(va)==ntos_mm::Access::read_write;
        },&table);
}
static nt::Status native_vm_reserve(void *,uint32_t preferred,uint32_t pages,
                                    uint32_t *base) {
    return g_nt_process.vm.reserve(
        preferred,pages,base,&ntos_vm_collides,
        &ntos_hw::g_context.table);
}
static nt::Status native_vm_commit(void *,uint32_t base,uint32_t pages,
                                   uint32_t rights) {
    return ntos_vm_commit(base,pages,rights);
}
static nt::Status native_vm_protect(void *,uint32_t base,uint32_t pages,
                                    uint32_t rights,uint32_t *old) {
    const nt::Status s=g_nt_process.vm.protect(
        base,pages,static_cast<nt::VmAccess>(rights),
        &ntos_vm_update_ptel,nullptr,old);
    if(s==nt::INTERNAL_ERROR)g_fatal.reason=3u;
    return s;
}
// CPU-backed invalidation: single core, interrupts masked across P4 write.
// The pager metadata is cleared ONLY after both UTLB+ITLB readback succeeds.
static bool vm_hw_invalidate(void *,unsigned,uint32_t va,uint32_t) {
    return mmu::invalidate_va_verified(va);
}
static bool vm_shootdown(void *,uint32_t va) {
    auto &vm=g_nt_process.vm;
    const int ix=vm.find(va);
    if(ix<0 || vm.pages[ix].frame==0xFFFFu)return false;
    auto &pager=ntos_hw::g_context.table;
    const int slot=pager.lookup(va);
    // P0 and P1 can occupy different SH4A OC indices for the same
    // physical page. Never free the frame based on P1 purge alone.
    // Missing residency means P0 dirty-cache aliases cannot be proven
    // absent by this early-stage routine: fail CLOSED.
    if(slot<0 || slot>=static_cast<int>(ntos_fault::SLOTS))
        return false;
#if NTOS_VERIFY_UTLB_SLOTS
    uint32_t phys=0u;
    phys=vm.physical+uint32_t(vm.pages[ix].frame)*4096u;
    if(!mmu::verify_slot(static_cast<uint32_t>(slot),va,phys,
                        vm.query(va)==nt::VmAccess::read_only ?
                        mmu::PTEL_PAGE_READ_ONLY :
                        vm.query(va)==nt::VmAccess::emulator_user_denied ?
                        mmu::PTEL_PAGE_SUPERVISOR_ONLY :
                        mmu::PTEL_PAGE))
        return false;
#endif
    // First write back+invalidate the *guest P0 alias* while its
    // translation is still resident. Then purge the backing P1 alias.
    // If invalidation later fails, contents remain intact in RAM.
    mmu::cache_writeback_invalidate_block(
        reinterpret_cast<void *>(va),4096u);
    mmu::cache_writeback_invalidate_block(
        vm.backing+uint32_t(vm.pages[ix].frame)*4096u,4096u);
    return pager.invalidate(va,&vm_hw_invalidate,nullptr);
}
static nt::Status native_vm_decommit(void *,uint32_t base,uint32_t pages) {
#if NTOS_VERIFY_UTLB_SLOTS && NTOS_ENABLE_NT_PRIVATE_VM
    auto &ctx=ntos_hw::g_context;
    if(ntos_vm_busy() || !g_nt_process.space.valid() ||
       !g_nt_process.ke.running() ||
       ctx.service_armed || ctx.user_probe_session ||
       ctx.guest_guard.armed || ctx.table.servicing)
        return nt::DEVICE_BUSY;
    return g_nt_process.vm.decommit(base,pages,&vm_shootdown,nullptr);
#else
    (void)base;(void)pages;return nt::NOT_SUPPORTED;
#endif
}
static nt::Status native_vm_release(void *,uint32_t base) {
    return g_nt_process.vm.release(base);
}
static nt::NativeVmOps native_vm_hooks() {
    return {nullptr,&native_vm_reserve,&native_vm_commit,
                    &native_vm_protect,&native_vm_release,
                    &native_vm_decommit};
}
#endif
static uint32_t ntos_native_allocate(uint32_t process,uint32_t *base,
                                      uint32_t zero_bits,uint32_t *bytes,
                                      uint32_t type,uint32_t protection) {
#if NTOS_ENABLE_NT_PRIVATE_VM && NTOS_ENABLE_TLB_MISS_PAGER
    if(!native_guest_word(base) || !native_guest_word(bytes) ||
       base==bytes)return nt::INVALID_PARAMETER;
    return nt::nt_allocate_virtual_memory(
        native_vm_hooks(),process,base,zero_bits,bytes,type,protection);
#else
    (void)process;(void)base;(void)zero_bits;(void)bytes;
    (void)type;(void)protection;
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_native_protect(uint32_t process,uint32_t *base,
                                     uint32_t *bytes,uint32_t protection,
                                     uint32_t *old) {
#if NTOS_ENABLE_NT_PRIVATE_VM && NTOS_ENABLE_TLB_MISS_PAGER
    if(!native_guest_word(base) || !native_guest_word(bytes) ||
       !native_guest_word(old) || base==bytes || base==old ||
       bytes==old)return nt::INVALID_PARAMETER;
    return nt::nt_protect_virtual_memory(
        native_vm_hooks(),process,base,bytes,protection,old);
#else
    (void)process;(void)base;(void)bytes;(void)protection;(void)old;
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_native_free(uint32_t process,uint32_t *base,
                                  uint32_t *bytes,uint32_t type) {
#if NTOS_ENABLE_NT_PRIVATE_VM && NTOS_ENABLE_TLB_MISS_PAGER
    if(!native_guest_word(base) || !native_guest_word(bytes) ||
       base==bytes)return nt::INVALID_PARAMETER;
    return nt::nt_free_virtual_memory(
        native_vm_hooks(),process,base,bytes,type);
#else
    (void)process;(void)base;(void)bytes;(void)type;
    return nt::NOT_SUPPORTED;
#endif
}

static uint32_t ntos_ke_arm_system_call(uint32_t trap_pc,uint32_t service) {
#if NTOS_ENABLE_TLB_MISS_PAGER && NTOS_ENABLE_KE_USER_SYSCALL
#if NTOS_ENABLE_COOP_KERNEL
    if(g_ke_coop.created && g_ke_coop.active==1u)return nt::DEVICE_BUSY;
#endif
    if(!g_nt_process.active || !g_nt_process.ke.running())
        return nt::DEVICE_BUSY;
    return ntos_hw::arm_user_service(trap_pc,service);
#else
    (void)trap_pc;(void)service;
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_ps_get_current_process_id(void) {
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(g_ps_root.twin && g_ps_root.ready && g_ps_root.active==1u &&
       g_ps_root.child.active)
        return g_ps_root.child.unique_process_id;
#endif
    return g_nt_process.ke.process.active ?
        g_nt_process.ke.process.unique_process_id : 0u;
}
static uint32_t ntos_ps_get_current_thread_id(void) {
#if NTOS_ENABLE_COOP_KERNEL
    return g_ke_coop.tid(g_nt_process.ke);
#else
    return g_nt_process.ke.running() ?
        g_nt_process.ke.thread.unique_thread_id : 0u;
#endif
}
// Temporary, C ABI-only Ke cooperative bridge: same KPROCESS and mapped
// PE image, no virtual root switch. A future full trap-frame switch is
// required for arbitrary user mode, DSP and timer preemption.
static uint32_t ntos_ke_create_coop_thread(uint32_t entry,uint32_t sp) {
#if NTOS_ENABLE_COOP_KERNEL && NTOS_ENABLE_TLB_MISS_PAGER
    auto &ctx=ntos_hw::g_context;
    if(!g_nt_process.active || !g_nt_process.ke.running() ||
       !g_nt_process.space.valid() || g_ke_coop.created ||
       ctx.service_armed || ctx.user_probe_session ||
       ctx.guest_guard.armed || !ctx.installed || !mmu::at_enabled() ||
       !entry || entry>=0x80000000u || (entry&1u) ||
       sp<4096u || sp>=0x80000000u || (sp&15u) ||
       !ntos_hw::executable_page(&ctx.table,entry) ||
       !ntos_hw::executable_page(&ctx.table,entry+2u) ||
       ctx.table.lookup(sp-4u)<0 ||
       ctx.table.access(sp-4u)!=ntos_mm::Access::read_write)
        return 0u;
    uint32_t sr=0u,gbr=0u;
    __asm__ volatile("stc sr,%0\n\tstc gbr,%1"
                     : "=r"(sr),"=r"(gbr) :: "memory");
    uint32_t tid=0u;
    if(g_ke_coop.create(g_nt_process.ke,entry,sp,sr,gbr,&tid)
       !=nt::SUCCESS)return 0u;
    g_nt_process.ke.process.threads=2u;
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(g_ps_root.twin && g_ps_root.ready && !g_ps_root.child.active) {
        g_ps_root.child.unique_process_id=
            g_nt_process.ke.process.unique_process_id+1u;
        g_ps_root.child.inherited_from_process_id=
            g_nt_process.ke.process.unique_process_id;
        g_ps_root.child.image_base=g_pe_pair.base;
        g_ps_root.child.image_bytes=4096u; // test-only PE image identity
        g_ps_root.child.threads=1u;
        g_ps_root.child.active=true;
#if NTOS_ENABLE_CHILD_PS_RESOURCES
        if(g_ps_child_process && g_ps_child_process->active) {
            g_ps_child_process->ke.process=g_ps_root.child;
            g_ps_child_process->ke.thread.owning_process_id=
                g_ps_root.child.unique_process_id;
            g_ps_child_process->ke.thread.unique_thread_id=tid;
            g_ps_child_process->ke.thread.start_address=g_pe_pair.entry;
            g_ps_child_process->ke.thread.state=nt::KTHREAD_STATE::ready;
        }
#endif
        g_ke_coop.peer.owning_process_id=
            g_ps_root.child.unique_process_id;
    }
#endif
    return tid;
#else
    (void)entry;(void)sp;return 0u;
#endif
}
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
// Root handoff is invoked from a normal privileged P1 Ke service, NEVER
// on the VBR fault stack. Only one mapped P0 DATA page is replaced.
// Cache writeback must precede loss of the guest P0 alias; API window,
// text, stacks, VBR, and host pages are never invalidated here.
static nt::Status psroot_activate(uint32_t target) {
#if NTOS_VERIFY_UTLB_SLOTS
    if(target>1u || !g_ps_root.ready || !g_nt_process.ke.running() ||
       !mmu::at_enabled())return nt::INVALID_PARAMETER;
    if(g_ps_root.active==target)return nt::SUCCESS;
    auto &ctx=ntos_hw::g_context;
    auto &pager=ctx.table;
    const uint32_t va=g_ps_root.va;
    const unsigned before=g_ps_root.active;
    const int slot=pager.lookup(va);
    if(!ctx.installed || ctx.service_armed || ctx.user_probe_session ||
       ctx.guest_guard.armed || pager.servicing ||
       g_ps_root.module_index>=pager.modules_used ||
       pager.modules[g_ps_root.module_index].va!=va ||
       pager.modules[g_ps_root.module_index].pa!=g_ps_root.pa[before] ||
       slot<0 || !mmu::verify_slot(static_cast<uint32_t>(slot),
                                   va,g_ps_root.pa[before]))
        return nt::DEVICE_BUSY;
#if NTOS_ENABLE_PS_EXEC
    const unsigned code_pages=g_ps_root.exec_pages;
    if(code_pages) {
        if(code_pages>8u ||
           g_ps_root.exec_module_index>=pager.modules_used ||
           pager.modules[g_ps_root.exec_module_index].va!=
               g_ps_root.exec_va ||
           pager.modules[g_ps_root.exec_module_index].pa!=
               g_ps_root.exec_pa[before] ||
           pager.modules[g_ps_root.exec_module_index].pages!=code_pages)
            return nt::DEVICE_BUSY;
        for(uint32_t i=0u;i<code_pages;++i) {
            const uint32_t code_va=g_ps_root.exec_va+i*4096u;
            const int index=pager.lookup(code_va);
            const uint32_t rights=pager.access(code_va)==
                     ntos_mm::Access::read_only ?
                     mmu::PTEL_PAGE_READ_ONLY : mmu::PTEL_PAGE;
            if(index<0 || !mmu::verify_slot(
                   static_cast<uint32_t>(index),code_va,
                   g_ps_root.exec_pa[before]+i*4096u,rights))
                return nt::DEVICE_BUSY;
        }
    }
#endif
    // The entire old-alias writeback / verified UTLB+ITLB removal /
    // new-root reinstall is one IRQ-excluded transaction. Never call
    // SDK, heap or host file functions during this privileged interval.
    uint32_t previous=0u;
    __asm__ volatile("stc sr,%0" : "=r"(previous) :: "memory");
    if((previous&0x70000000u)!=0x40000000u)
        return nt::DEVICE_BUSY;
    const uint32_t masked=previous|0xF0u;
    __asm__ volatile("ldc %0,sr\n\tnop" :: "r"(masked) : "memory");
    const nt::Status result=[&]() -> nt::Status {
        auto invalidate=[](void *,unsigned,uint32_t addr,uint32_t) {
            return mmu::invalidate_va_verified(addr);
        };
        // On any partial P4 failure, recover ALL old pages before
        // allowing guest code to execute. PID/root is still unpublished.
        auto recover=[&]() -> nt::Status {
#if NTOS_ENABLE_PS_EXEC
            for(uint32_t i=0u;i<code_pages;++i) {
                const uint32_t code_va=g_ps_root.exec_va+i*4096u;
                if(!pager.invalidate(code_va,invalidate,nullptr)) {
                    g_fatal.reason=4u;return nt::INTERNAL_ERROR;
                }
            }
            if(code_pages)
                pager.modules[g_ps_root.exec_module_index].pa=
                    g_ps_root.exec_pa[before];
#endif
            if(!pager.invalidate(va,invalidate,nullptr)) {
                g_fatal.reason=4u;return nt::INTERNAL_ERROR;
            }
            pager.modules[g_ps_root.module_index].pa=g_ps_root.pa[before];
#if NTOS_ENABLE_PS_EXEC
            for(uint32_t i=0u;i<code_pages;++i) {
                const uint32_t code_va=g_ps_root.exec_va+i*4096u;
                if(!ntos_hw::prime(code_va,true)) {
                    g_fatal.reason=4u;return nt::INTERNAL_ERROR;
                }
                const int code_slot=pager.lookup(code_va);
                const uint32_t flags=pager.access(code_va)==
                           ntos_mm::Access::read_only ?
                           mmu::PTEL_PAGE_READ_ONLY : mmu::PTEL_PAGE;
                if(code_slot<0 || !mmu::verify_slot(
                        static_cast<unsigned>(code_slot),code_va,
                        g_ps_root.exec_pa[before]+i*4096u,flags)) {
                    g_fatal.reason=4u;return nt::INTERNAL_ERROR;
                }
            }
#endif
            if(!ntos_hw::prime(va,true)) {
                g_fatal.reason=4u;return nt::INTERNAL_ERROR;
            }
            const int old_slot=pager.lookup(va);
            if(old_slot<0 || !mmu::verify_slot(
                   static_cast<unsigned>(old_slot),va,g_ps_root.pa[before])) {
                g_fatal.reason=4u;return nt::INTERNAL_ERROR;
            }
            return nt::DEVICE_BUSY;
        };
#if NTOS_ENABLE_PS_EXEC
        // Purge BOTH guest P0 instruction-cache and P1 backing aliases
        // before the physical text/data root changes at the SAME VPN.
        for(uint32_t i=0u;i<code_pages;++i) {
            const uint32_t code_va=g_ps_root.exec_va+i*4096u;
            mmu::cache_writeback_invalidate_block(
                reinterpret_cast<void *>(code_va),4096u);
            mmu::cache_flush_invalidate_block(
                reinterpret_cast<void *>(code_va),4096u);
            mmu::cache_writeback_invalidate_block(
                g_ps_root.exec_backing[before]+i*4096u,4096u);
        }
        for(uint32_t i=0u;i<code_pages;++i)
            if(!pager.invalidate(g_ps_root.exec_va+i*4096u,
                                 invalidate,nullptr))
                return recover();
#endif
        mmu::cache_writeback_invalidate_block(
            reinterpret_cast<void *>(va),4096u);
        mmu::cache_writeback_invalidate_block(
            g_ps_root.backing[before],4096u);
        if(!pager.invalidate(va,invalidate,nullptr))return recover();
#if NTOS_ENABLE_PS_EXEC
        if(code_pages)
            pager.modules[g_ps_root.exec_module_index].pa=
                g_ps_root.exec_pa[target];
#endif
        pager.modules[g_ps_root.module_index].pa=g_ps_root.pa[target];
#if NTOS_ENABLE_PS_EXEC
        for(uint32_t i=0u;i<code_pages;++i) {
            const uint32_t code_va=g_ps_root.exec_va+i*4096u;
            if(!ntos_hw::prime(code_va,true))return recover();
            const int new_slot=pager.lookup(code_va);
            const uint32_t flags=pager.access(code_va)==
                        ntos_mm::Access::read_only ?
                        mmu::PTEL_PAGE_READ_ONLY : mmu::PTEL_PAGE;
            if(new_slot<0 || !mmu::verify_slot(
                   static_cast<unsigned>(new_slot),code_va,
                   g_ps_root.exec_pa[target]+i*4096u,flags))
                return recover();
        }
#endif
        if(!ntos_hw::prime(va,true))return recover();
        const int new_slot=pager.lookup(va);
        if(new_slot<0 ||
           !mmu::verify_slot(static_cast<unsigned>(new_slot),
                             va,g_ps_root.pa[target]))
            return recover();
        // Only now publish the new process root and its private P0 data.
        g_ps_root.active=target;
        ++g_ps_root.switches;
        return nt::SUCCESS;
    }();
    __asm__ volatile("ldc %0,sr\n\tnop" :: "r"(previous) : "memory");
    return result;
#else
    (void)target;return nt::NOT_SUPPORTED;
#endif
}
#endif
static uint32_t ntos_psroot_va(void) {
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    return g_ps_root.ready && g_nt_process.ke.running() ?
           g_ps_root.va : 0u;
#else
    return 0u;
#endif
}
static uint32_t ntos_psroot_current(void) {
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    return g_ps_root.ready ? g_ps_root.active : 0xffffffffu;
#else
    return 0xffffffffu;
#endif
}
static uint32_t ntos_psroot_switch(uint32_t target) {
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    // PSTWIN root changes belong exclusively to Ke's scheduler.
    if(g_ps_root.twin)return nt::DEVICE_BUSY;
    return psroot_activate(target);
#else
    (void)target;return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_io_open_handle_count(void) {
#if NTOS_ENABLE_GUEST_IO
    return g_nt_io.enabled && g_nt_process.ke.running() ?
        g_nt_io.manager.open_count : 0xffffffffu;
#else
    return 0xffffffffu;
#endif
}
static uint32_t ntos_ps_user_stack_top(void) {
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    return g_ps_user.ready && g_nt_process.ke.running() ?
           g_ps_user.va+4096u : 0u;
#else
    return 0u;
#endif
}
static uint32_t ntos_ps_user_arm(uint32_t loop_pc,uint32_t exit_pc) {
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    if(!g_ps_user.ready || !g_nt_process.ke.running())
        return nt::DEVICE_BUSY;
#if NTOS_ENABLE_COOP_KERNEL
    if(g_ke_coop.created)return nt::DEVICE_BUSY;
#endif
    return ntos_hw::arm_user_loop(
        loop_pc,exit_pc,g_ps_user.va,g_ps_user.va+4096u);
#else
    (void)loop_pc;(void)exit_pc;return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_ps_user_syscalls(void) {
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    return ntos_hw::g_context.user_loop_calls;
#else
    return 0u;
#endif
}
static uint32_t ntos_ps_exec_entry(void) {
#if NTOS_ENABLE_PS_EXEC
    return g_ps_root.ready && g_ps_root.exec_pages &&
           g_nt_process.ke.running() ? g_ps_root.exec_entry : 0u;
#else
    return 0u;
#endif
}
static uint32_t ntos_pstwin_child_pid(void) {
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    return g_ps_root.twin && g_ps_root.child.active ?
           g_ps_root.child.unique_process_id : 0u;
#else
    return 0u;
#endif
}
static uint32_t ntos_ke_coop_yield(void) {
#if NTOS_ENABLE_COOP_KERNEL && NTOS_ENABLE_TLB_MISS_PAGER
    auto &ctx=ntos_hw::g_context;
    if(ctx.service_armed || ctx.user_probe_session ||
       ctx.guest_guard.armed || !mmu::at_enabled() ||
       g_nt_process.ke.thread.exceptions.depth)
        return nt::DEVICE_BUSY;
    if(!g_ke_coop.created || g_ke_coop.exited)return nt::SUCCESS;
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(g_ps_root.twin) {
        const nt::Status gate=psroot_activate(g_ke_coop.active^1u);
        if(gate!=nt::SUCCESS)return gate;
    }
#endif
    nt::CoopSaved *from=nullptr;const nt::CoopSaved *to=nullptr;
    if(!g_ke_coop.next(g_nt_process.ke,&from,&to))
        return nt::DEVICE_BUSY;
    ntos_ke_coop_swap(from,to);
    return nt::SUCCESS;
#else
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_ke_coop_exit(void) {
#if NTOS_ENABLE_COOP_KERNEL && NTOS_ENABLE_TLB_MISS_PAGER
    auto &ctx=ntos_hw::g_context;
    if(ctx.service_armed || ctx.user_probe_session ||
       ctx.guest_guard.armed || !mmu::at_enabled())
        return nt::DEVICE_BUSY;
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(g_ps_root.twin && psroot_activate(0u)!=nt::SUCCESS)
        return nt::DEVICE_BUSY;
#if NTOS_ENABLE_GUEST_IO
    // Revoke every child-owned FILE_OBJECT while still on its P1 Ke
    // exit path. Io close only carries handles, never guest pointers.
    // Parent objects and driver mappings must survive the child death.
    if(g_ps_root.twin && g_ps_root.child.active &&
       g_nt_io.manager.close_owner(
           g_ps_root.child.unique_process_id)!=nt::SUCCESS)
        return nt::DEVICE_BUSY;
#endif
    if(g_ps_root.twin && g_ps_root.child.active) {
#if NTOS_ENABLE_CHILD_PS_RESOURCES
        if(g_ps_child_process && g_ps_child_process->active)
            g_ps_child_process->finish();
#endif
        g_ps_root.child.active=false;
    }
#endif
    nt::CoopSaved *from=nullptr;const nt::CoopSaved *to=nullptr;
    if(!g_ke_coop.exit_peer(g_nt_process.ke,&from,&to))
        return nt::INVALID_PARAMETER;
    g_nt_process.ke.process.threads=1u;
    ntos_ke_coop_swap(from,to);
    return nt::INTERNAL_ERROR; // secondary must never resume
#else
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_ke_coop_switch_count(void) {
#if NTOS_ENABLE_COOP_KERNEL
    return g_nt_process.ke.running() ? g_ke_coop.switches : 0u;
#else
    return 0u;
#endif
}
// I/O fast-path services are normal privileged C-ABI calls, NOT VBR IRQs.
// Every guest pointer is checked against its resident P0 page + rights
// BEFORE dereference. No SDK file handles or direct SH7305 MMIO here.
#if NTOS_ENABLE_GUEST_IO
static bool nt_io_range(const void *address,uint32_t bytes,bool writable) {
    if(!g_nt_process.active || !g_nt_process.space.valid() ||
       !ntos_hw::g_context.installed || !mmu::at_enabled() ||
       !address || !bytes || bytes>4096u)return false;
    const uint32_t v=static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(address));
    if(v<0x1000u || v>=0x80000000u || bytes>0x80000000u-v)
        return false;
    auto &pager=ntos_hw::g_context.table;
    const uint32_t last=v+bytes-1u;
    for(uint32_t page=v&~0xFFFu;page<=last;page+=4096u) {
        if(pager.lookup(page)<0)return false;
        const auto a=pager.access(page);
        if(writable ? a!=ntos_mm::Access::read_write :
           (a!=ntos_mm::Access::read_only &&
            a!=ntos_mm::Access::read_write))
            return false;
        if(page>=0x7FFFF000u)break;
    }
    return true;
}
static bool nt_io_check(void *,const void *buffer,uint32_t bytes,bool write) {
    return nt_io_range(buffer,bytes,write);
}
static uint32_t nt_io_pid() {
    return g_nt_process.ke.running() && g_nt_process.space.valid() ?
        ntos_ps_get_current_process_id() : 0u;
}
#endif
static uint32_t ntos_nt_open_device(const char *name,uint32_t *out) {
#if NTOS_ENABLE_GUEST_IO
    const uint32_t pid=nt_io_pid();
    if(!pid || !g_nt_io.enabled || !out ||
       (reinterpret_cast<uintptr_t>(out)&3u) ||
       !nt_io_range(out,4u,true) || !name)return nt::INVALID_PARAMETER;
    char safe[40]{};
    for(unsigned i=0u;i<sizeof(safe);++i) {
        if(!nt_io_range(name+i,1u,false))return nt::INVALID_PARAMETER;
        safe[i]=name[i];
        if(safe[i]==0)return g_nt_io.manager.open(pid,safe,out);
    }
    return nt::INVALID_PARAMETER;
#else
    (void)name;(void)out;return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_nt_read_device(uint32_t h,void *buffer,uint32_t bytes,
                                     uint32_t *done) {
#if NTOS_ENABLE_GUEST_IO
    const uint32_t pid=nt_io_pid();
    if(!pid || !done || (reinterpret_cast<uintptr_t>(done)&3u) ||
       !nt_io_range(done,4u,true) || !bytes || bytes>64u)
        return nt::INVALID_PARAMETER;
    nt::io::IoStatusBlock io{};
    const nt::Status status=g_nt_io.manager.request(
        pid,h,nt::io::Major::read,buffer,bytes,0u,&io,&nt_io_check,nullptr);
    if(status==nt::SUCCESS)*done=io.information;
    return status;
#else
    (void)h;(void)buffer;(void)bytes;(void)done;
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_nt_write_device(uint32_t h,const void *buffer,
                                      uint32_t bytes,uint32_t *done) {
#if NTOS_ENABLE_GUEST_IO
    const uint32_t pid=nt_io_pid();
    if(!pid || !done || (reinterpret_cast<uintptr_t>(done)&3u) ||
       !nt_io_range(done,4u,true) || !bytes || bytes>64u)
        return nt::INVALID_PARAMETER;
    nt::io::IoStatusBlock io{};
    const nt::Status status=g_nt_io.manager.request(
        pid,h,nt::io::Major::write,const_cast<void *>(buffer),bytes,0u,
        &io,&nt_io_check,nullptr);
    if(status==nt::SUCCESS)*done=io.information;
    return status;
#else
    (void)h;(void)buffer;(void)bytes;(void)done;
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_nt_close_device(uint32_t h) {
#if NTOS_ENABLE_GUEST_IO
    const uint32_t pid=nt_io_pid();
    return pid ? g_nt_io.manager.close(pid,h) : nt::DEVICE_BUSY;
#else
    (void)h;return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_nt_device_io_control(uint32_t h,uint32_t code,
                                        void *buffer,uint32_t bytes,
                                        uint32_t *done) {
#if NTOS_ENABLE_GUEST_IO
    const uint32_t pid=nt_io_pid();
    if(!pid || !done || (reinterpret_cast<uintptr_t>(done)&3u) ||
       !nt_io_range(done,4u,true) || !bytes || bytes>64u)
        return nt::INVALID_PARAMETER;
    nt::io::IoStatusBlock io{};
    const nt::Status status=g_nt_io.manager.request(
        pid,h,nt::io::Major::device_control,buffer,bytes,code,&io,
        &nt_io_check,nullptr);
    if(status==nt::SUCCESS)*done=io.information;
    return status;
#else
    (void)h;(void)code;(void)buffer;(void)bytes;(void)done;
    return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_io_replace_loop_driver(uint32_t variant) {
#if NTOS_ENABLE_GUEST_IO
    return nt_io_pid() ? g_nt_io.replace_loop(variant) : nt::DEVICE_BUSY;
#else
    (void)variant;return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_ps_peer_entry(void) {
#if NTOS_ENABLE_PE_PAIR && NTOS_ENABLE_COOP_KERNEL
    return g_pe_pair.ready && g_nt_process.ke.running() &&
           g_pe_pair.entry!=0u ? g_pe_pair.entry : 0u;
#else
    return 0u;
#endif
}
static uint32_t ntos_ps_peer_tick(void) {
#if NTOS_ENABLE_PE_PAIR && NTOS_ENABLE_COOP_KERNEL
    if(!g_pe_pair.ready || !g_ke_coop.created ||
       g_ke_coop.active!=1u || g_pe_pair.progress>=500u)
        return 0u;
    return ++g_pe_pair.progress;
#else
    return 0u;
#endif
}
static uint32_t ntos_ps_peer_progress(void) {
#if NTOS_ENABLE_PE_PAIR && NTOS_ENABLE_COOP_KERNEL
    return g_pe_pair.ready && g_nt_process.ke.running() ?
        g_pe_pair.progress : 0u;
#else
    return 0u;
#endif
}
static uint32_t ntos_ke_system_service_count(void) {
    return g_nt_process.ke.process.active ?
        g_nt_process.ke.thread.syscall_count : 0u;
}
static uint32_t ntos_ke_register_exception(uint32_t tea,uint32_t ev,
                                            uint32_t pc,uint32_t resume) {
#if NTOS_ENABLE_KE_USER_SYSCALL && NTOS_ENABLE_GUEST_PROTECT_DISPATCH
    if(!g_nt_process.active || !g_nt_process.space.valid())return 0u;
#if NTOS_ENABLE_COOP_KERNEL
    if(g_ke_coop.created && g_ke_coop.active==1u)return 0u;
#endif
    return ntos_hw::arm_thread_exception(tea,ev,pc,resume);
#else
    (void)tea;(void)ev;(void)pc;(void)resume;return 0u;
#endif
}
static uint32_t ntos_ke_cancel_exception(uint32_t token) {
#if NTOS_ENABLE_KE_USER_SYSCALL && NTOS_ENABLE_GUEST_PROTECT_DISPATCH
    auto &c=ntos_hw::g_context;
    if(!g_nt_process.ke.running())return nt::DEVICE_BUSY;
    if(c.user_probe_session)return nt::DEVICE_BUSY;
    return g_nt_process.ke.unregister_exception(token);
#else
    (void)token;return nt::NOT_SUPPORTED;
#endif
}
static uint32_t ntos_ke_exception_delivered(void) {
    return g_nt_process.ke.running() ?
        g_nt_process.ke.thread.exceptions.delivered : 0u;
}
static uint32_t ntos_ke_exception_status(void) {
    return g_nt_process.ke.running() ?
        g_nt_process.ke.thread.exceptions.last.code : 0u;
}
static uint32_t ntos_ke_exception_address(void) {
    return g_nt_process.ke.running() ?
        g_nt_process.ke.thread.exceptions.last.fault_address : 0u;
}
static uint32_t ntos_ke_exception_depth(void) {
    return g_nt_process.ke.running() ?
        g_nt_process.ke.thread.exceptions.depth : 0u;
}
static uint32_t ntos_ps_address_space_id(void) {
    return g_nt_process.space.valid() ?
           ntos_ps_get_current_process_id() : 0u;
}
static uint32_t ntos_ps_address_space_epoch(void) {
    return g_nt_process.space.valid() ? g_nt_process.space.epoch : 0u;
}
static uint32_t ntos_ps_inherited_from_process_id(void) {
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(g_ps_root.twin && g_ps_root.active==1u &&
       g_ps_root.child.active)
        return g_ps_root.child.inherited_from_process_id;
#endif
    return g_nt_process.ke.process.active ?
        g_nt_process.ke.process.inherited_from_process_id : 0u;
}

// The eager mapper and the future demand pager share a file-to-page
// materializer. In the old mode this Reader uses a complete P1 RAM buffer;
// in streamed mode the source is an SDK file read with MMU/VBR restored.
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
#if NTOS_STREAMED_IMAGE_LOADING
// Bounded random-access SDK reads. Only called while running in the host's
// P1 context, with AT disabled and NTOS not owning VBR. Never use this
// callback to service a hardware page fault.
struct PeDiskReader {
    int fd;
    uint32_t size;
};
static bool pe_disk_read(void *context,uint32_t position,
                         uint8_t *destination,uint32_t count) {
    auto *source=static_cast<PeDiskReader *>(context);
    if(!source || !destination || !g_fs.file_is(source->fd) ||
       position>source->size || count>source->size-position ||
       position>0x7fffffffu) return false;
    if(File_Lseek(source->fd,static_cast<int32_t>(position),
                  FILE_SEEK_SET)!=static_cast<int32_t>(position))
        return false;
    uint32_t completed=0u;
    while(completed<count) {
        uint32_t wanted=count-completed;
        if(wanted>4096u)wanted=4096u;
        const int got=File_Read(source->fd,destination+completed,
                                static_cast<int>(wanted));
        if(got<=0 || static_cast<uint32_t>(got)>wanted)return false;
        completed+=static_cast<uint32_t>(got);
    }
    return true;
}
#endif

// Materialize a complete resident image directly from a bounded file source.
// This intentionally changes only file staging: the current fully RAM-backed
// TLB-miss pager, relocations and DLL import behavior remain unchanged.
static int load_mapped_reader(ntos_pager::ReadAt read,void *reader_context,
                              uint32_t file_size,const pe::Image &hdr,
                              MappedImage *out_mi,const char **err) {
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
    ntos_pager::Source src{&hdr,reader_context,read,file_size,
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
static int load_mapped(const uint8_t *file,uint32_t file_size,
                       const pe::Image &hdr,MappedImage *out_mi,
                       const char **err) {
    PeMemoryReader source{file,file_size};
    return load_mapped_reader(&pe_memory_read,&source,file_size,hdr,out_mi,err);
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
    /* WinCE Control Panel applets are executable PE DLL modules with .CPL
     * filenames; reject all other suffixes and directory traversal. */
    return module_matches(suffix,".DLL") || module_matches(suffix,".CPL");
}

static bool dll_path(char *dst,uint32_t capacity,const char *name) {
    static const char root[]="\\fls0\\Windows\\System32\\";
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
    int fd=fs_open(path,FILE_OPEN_READ);
    // Compatibility fallback for older calculator installs with flat DLLs.
    // System32 wins; never attempt fallback while another SDK handle is open.
    if(fd<0 && !g_fs.busy()) {
        char legacy[64]="\\fls0\\";
        unsigned k=6;
        const char *last=path;
        for(unsigned i=0;path[i];++i)
            if(path[i]=='\\')last=path+i+1u;
        for(unsigned i=0;last[i] && k+1<sizeof(legacy);++i)
            legacy[k++]=last[i];
        legacy[k]=0;
        fd=fs_open(legacy,FILE_OPEN_READ);
    }
    if(fd<0){ *error="dependency absent from System32 and fls0";return false; }
    struct File_Stat st{};
    if(File_Fstat(fd,&st)<0 || st.fileSize<512u ||
       st.fileSize>NTOS_MAX_FILE_BYTES) {
        *error="invalid DLL length or exceeds staging file limit";
        fs_close(fd);return false;
    }
    const uint32_t n=static_cast<uint32_t>(st.fileSize);
    uint8_t *buf=static_cast<uint8_t *>(Mem_Malloc(n));
    if(!buf) {
        *error="out of memory staging DLL";
        fs_close(fd);return false;
    }
    uint32_t off=0u;
    while(off<n) {
        uint32_t wanted=n-off;
        if(wanted>4096u) wanted=4096u;
        int count=File_Read(fd,buf+off,static_cast<int>(wanted));
        if(count<=0 || static_cast<uint32_t>(count)>wanted) break;
        off+=static_cast<uint32_t>(count);
    }
    if(fs_close(fd)<0) {
        Mem_Free(buf);
        *error="SDK dependency file close failed; filesystem locked";
        return false;
    }
    if(off!=n) {
        Mem_Free(buf);*error="short read staging dependency";return false;
    }
    *dst=buf;*bytes=n;return true;
}

#if NTOS_STREAMED_IMAGE_LOADING
static uint32_t g_skipped_pe_staging_bytes=0u;
// Load and close each module before discovering the next DLL. The host's
// filesystem gate allows only one SDK descriptor at a time. No file can
// remain open when the MMU/VBR ownership changes or the payload runs.
// If mapped==nullptr, validate only headers (for the launcher picker).
static bool read_pe_source(const char *path,bool flat_dll_fallback,
                           bool must_be_dll,pe::Image *header,
                           MappedImage *mapped,uint32_t *size,
                           const char **err) {
    if(!path || !header || !size || !err)return false;
    *size=0u;
    int fd=fs_open(path,FILE_OPEN_READ);
    if(fd<0 && flat_dll_fallback && !g_fs.busy()) {
        char legacy[64]="\\fls0\\";
        unsigned k=6;
        const char *last=path;
        for(unsigned i=0;path[i];++i)
            if(path[i]=='\\')last=path+i+1u;
        for(unsigned i=0;last[i] && k+1<sizeof(legacy);++i)
            legacy[k++]=last[i];
        legacy[k]=0;
        fd=fs_open(legacy,FILE_OPEN_READ);
    }
    if(fd<0) {*err="PE file unavailable or filesystem busy";return false;}
    bool valid=false;
    struct File_Stat st{};
    do {
        if(File_Fstat(fd,&st)<0 || st.fileSize<512u ||
           st.fileSize>NTOS_MAX_FILE_BYTES) {
            *err="invalid PE file length";
            break;
        }
        const uint32_t bytes=static_cast<uint32_t>(st.fileSize);
        PeDiskReader source{fd,bytes};
        if(pe_stream::parse(&pe_disk_read,&source,bytes,header)!=0) {
            *err="bad or unsupported SH4BE PE header";
            break;
        }
        if(must_be_dll && !(header->fh.characteristics&pe::F_DLL)) {
            *err="dependency is not an SH4BE DLL";
            break;
        }
        if(mapped && load_mapped_reader(&pe_disk_read,&source,bytes,
                                        *header,mapped,err)!=0)
            break;
        *size=bytes;
        valid=true;
    } while(false);
    if(fs_close(fd)<0) {
        *err="SDK PE file close failed; refusing further I/O";
        valid=false;
    }
    if(!valid && mapped && mapped->raw) {
        Mem_Free(mapped->raw);
        *mapped=MappedImage{};
    }
    if(valid && mapped)g_skipped_pe_staging_bytes+=*size;
    return valid;
}
#endif

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
#if NTOS_STREAMED_IMAGE_LOADING
    if(!read_pe_source(path,true,true,&module.hdr,&module.mapped,
                       &module.file_bytes,err))
        return false;
#else
    if(!read_module_file(path,&module.file,&module.file_bytes,err))
        return false;
    if(pe::parse(module.file,module.file_bytes,&module.hdr)!=0 ||
       !(module.hdr.fh.characteristics&pe::F_DLL)) {
        *err="dependency is not a valid SH4BE PE DLL";return false;
    }
    if(load_mapped(module.file,module.file_bytes,module.hdr,
                   &module.mapped,err)!=0)
        return false;
#endif
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
                      int *out_ret, const char **err,
                      const char *source_path=nullptr,
                      uint32_t parent_pid=0u) {
    (void)core_path;(void)core_size;(void)extra_path;(void)extra_size;
    *err="unknown";
    (void)source_path; // used only by the streamed-build variant
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
    if(!nt_process_configure()) {
        *err="cannot allocate P1 Ps-owned foreground Process";return -1;
    }
#if NTOS_ENABLE_PE_PAIR
    g_pe_pair=PePairState{};
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    g_ps_user=PsUserStack{};
#endif
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    g_ps_root=PsRootHardware{};
#endif
    if(!g_nt_process.begin()) {
        *err="another NT process still owns resources";return -1;
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
    uint8_t *vm_raw=nullptr;
#if NTOS_ENABLE_CHILD_PS_RESOURCES
    g_ps_child_process=nullptr;
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    uint8_t *ps_user_raw=nullptr;
    uint32_t ps_user_va=0u;
    bool user_requested=source_path &&
        ntos_launch_policy::ascii_equal_ci(source_path,
                                           "\\fls0\\PSUSER.EXE");
#endif
#if NTOS_ENABLE_PE_PAIR
    MappedImage second_exe{};
    pe::Image second_hdr{};
    uint32_t second_file_size=0u;
    uint32_t second_entry=0u;
    bool twin_requested=source_path &&
        ntos_launch_policy::ascii_equal_ci(source_path,
                                           "\\fls0\\PSTWIN.EXE");
    bool kill_requested=source_path &&
        ntos_launch_policy::ascii_equal_ci(source_path,
                                           "\\fls0\\PSKILL.EXE");
    bool pair_requested=source_path &&
        (ntos_launch_policy::ascii_equal_ci(source_path,
                                            "\\fls0\\PEPAIR.EXE") ||
         twin_requested || kill_requested);
#endif
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    uint8_t *psroot_raw=nullptr;
    uint32_t psroot_va=0u;
#if NTOS_ENABLE_PS_EXEC
    MappedImage exec_images[2]{};
    pe::Image exec_headers[2]{};
    uint32_t exec_sizes[2]{};
    uint32_t exec_va=0u;
    bool exec_requested=source_path &&
        ntos_launch_policy::ascii_equal_ci(source_path,
                                           "\\fls0\\PSCODE.EXE");
#endif
    bool root_requested=source_path &&
        (ntos_launch_policy::ascii_equal_ci(source_path,
                                            "\\fls0\\PSROOT.EXE") ||
         ntos_launch_policy::ascii_equal_ci(source_path,
                                            "\\fls0\\PSSTACK.EXE")
#if NTOS_ENABLE_PE_PAIR
         || twin_requested || kill_requested
#endif
#if NTOS_ENABLE_PS_EXEC
         || exec_requested
#endif
        );
#endif
    nt::DllLifecycle<NTOS_MAX_MODULES> dll_plan{};
    int rc=-1;

#if NTOS_STREAMED_IMAGE_LOADING
    g_skipped_pe_staging_bytes=0u;
#endif
    nodes[0].hdr=hdr;
    nodes[0].file=file;
    nodes[0].file_bytes=fsize;
    // Main EXE is owned by the caller; only dependency buffers are freed.
#if NTOS_STREAMED_IMAGE_LOADING
    if(source_path) {
        pe::Image disk_header{};
        uint32_t actual_size=0u;
        if(!read_pe_source(source_path,false,false,&disk_header,
                           &nodes[0].mapped,&actual_size,err))
            goto done;
        // The launcher opened this PE once already to choose the load mode.
        // Refuse a changed file instead of mixing two sets of metadata.
        if(actual_size!=fsize ||
           disk_header.oh.entry_rva!=hdr.oh.entry_rva ||
           disk_header.oh.image_base!=hdr.oh.image_base ||
           disk_header.oh.size_of_image!=hdr.oh.size_of_image ||
           disk_header.section_count!=hdr.section_count ||
           disk_header.oh.import_rva!=hdr.oh.import_rva ||
           disk_header.oh.import_size!=hdr.oh.import_size ||
           disk_header.oh.export_rva!=hdr.oh.export_rva ||
           disk_header.oh.export_size!=hdr.oh.export_size ||
           disk_header.oh.basereloc_rva!=hdr.oh.basereloc_rva ||
           disk_header.oh.basereloc_size!=hdr.oh.basereloc_size) {
            *err="PE headers changed while loading";
            goto done;
        }
        // A file replaced between the picker read and materialization
        // must not silently change PE section locations or permissions.
        for(uint32_t i=0u;i<hdr.section_count;++i) {
            const pe::SectionHeader &a=hdr.sections[i];
            const pe::SectionHeader &b=disk_header.sections[i];
            if(a.rva!=b.rva || a.vsize!=b.vsize ||
               a.raw_offset!=b.raw_offset || a.raw_size!=b.raw_size ||
               a.characteristics!=b.characteristics) {
                *err="PE sections changed while loading";
                goto done;
            }
        }
    } else
#endif
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

#if NTOS_ENABLE_PE_PAIR && NTOS_ENABLE_COOP_KERNEL && NTOS_STREAMED_IMAGE_LOADING
    if(pair_requested) {
        const char *second_name=kill_requested ?
            "\\fls0\\KILLCHILD.EXE" :
            twin_requested ? "\\fls0\\TWINCHILD.EXE" :
            "\\fls0\\PEPEER.EXE";
        if(!read_pe_source(second_name,false,false,
                           &second_hdr,&second_exe,&second_file_size,err))
            goto done;
        if((second_hdr.fh.characteristics&pe::F_DLL) ||
           second_hdr.oh.import_rva || !second_hdr.oh.entry_rva ||
           second_hdr.oh.size_of_image>65536u ||
           second_exe.npages>16u) {
            *err="PEPAIR second EXE must be import-free and <=64 KiB";
            goto done;
        }
    }
#elif NTOS_ENABLE_PE_PAIR
    if(pair_requested) {
        *err="PEPAIR needs streamed-loader build";
        goto done;
    }
#endif

#if NTOS_ENABLE_PS_EXEC && NTOS_STREAMED_IMAGE_LOADING
    if(exec_requested) {
        const char *paths[2]={"\\fls0\\CODEA.EXE","\\fls0\\CODEB.EXE"};
        for(unsigned i=0u;i<2u;++i) {
            if(!read_pe_source(paths[i],false,false,&exec_headers[i],
                                &exec_images[i],&exec_sizes[i],err))
                goto done;
            if((exec_headers[i].fh.characteristics&pe::F_DLL) ||
               exec_headers[i].oh.import_rva ||
               !exec_headers[i].oh.entry_rva ||
               !exec_images[i].npages ||
               exec_images[i].npages>8u) {
                *err="PSCODE requires small import-free executable PEs";
                goto done;
            }
        }
        if(exec_images[0].npages!=exec_images[1].npages ||
           exec_headers[0].oh.entry_rva!=exec_headers[1].oh.entry_rva) {
            *err="PSCODE executable layouts are incompatible";
            goto done;
        }
        for(uint32_t i=0u;i<exec_images[0].npages;++i)
            if(ntos_mm::pe_page_access(exec_headers[0],i)!=
               ntos_mm::pe_page_access(exec_headers[1],i)) {
                *err="PSCODE PE page permissions mismatch";
                goto done;
            }
    }
#elif NTOS_ENABLE_PS_EXEC
    if(exec_requested) {
        *err="PSCODE needs streamed loader";
        goto done;
    }
#endif

    // For <=55-page graphs, the experimental image uses the same fully
    // resident bootstrap as the proven regular loader. Unhinted small
    // programs (CHAINTEST, GUI) must not depend on an emulator delivering
    // missing-P0 access exceptions. Only larger graphs use the demand
    // pager, still requiring genuine VBR delivery or diagnostic hints.
    for(unsigned i=0u;i<count;++i)
        image_pages+=nodes[i].mapped.npages;
#if NTOS_ENABLE_PE_PAIR
    image_pages+=second_exe.npages;
#endif
#if NTOS_ENABLE_PS_EXEC
    image_pages+=exec_images[0].npages;
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    if(user_requested)++image_pages;
#endif
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(root_requested)++image_pages;
#endif
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
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(root_requested) {
        // Never repurpose an ordinary PE or private VM page for the
        // process-root canary. Both physical frames are allocated in
        // P1 before VBR ownership, and are never freed while AT is on.
        if(!ntos_fault::choose_virtual(0x00700000u,1u,
                                      reservations,count,&psroot_va)) {
            *err="no free P0 virtual slot for root canary";goto done;
        }
        psroot_raw=static_cast<uint8_t *>(Mem_Malloc(8192u+4095u));
        if(!psroot_raw) {
            *err="cannot allocate two physical root frames";goto done;
        }
        auto *aligned=static_cast<uint8_t *>(align_page(psroot_raw));
        Mem_Memset(aligned,0,8192u);
        g_ps_root.va=psroot_va;
        g_ps_root.backing[0]=aligned;
        g_ps_root.backing[1]=aligned+4096u;
        g_ps_root.pa[0]=mmu::phys_addr(aligned);
        g_ps_root.pa[1]=mmu::phys_addr(aligned+4096u);
#if NTOS_ENABLE_PE_PAIR
        g_ps_root.twin=twin_requested || kill_requested;
#endif
    }
#endif
#if NTOS_ENABLE_PS_EXEC
    if(exec_requested) {
        if(!ntos_fault::choose_virtual(0x00600000u,
                exec_images[0].npages,reservations,count,&exec_va) ||
           ntos_rebase::overlap(exec_va,exec_images[0].npages,
                                psroot_va,1u)) {
            *err="no private executable virtual mapping";goto done;
        }
        for(unsigned i=0u;i<2u;++i) {
            if(!ntos_rebase::apply(exec_headers[i],exec_images[i].image,
                         exec_images[i].vsize,exec_va,err))
                goto done;
            exec_images[i].base=exec_va;
            g_ps_root.exec_pa[i]=mmu::phys_addr(exec_images[i].image);
            g_ps_root.exec_backing[i]=exec_images[i].image;
        }
        g_ps_root.exec_pages=exec_images[0].npages;
        g_ps_root.exec_va=exec_va;
        g_ps_root.exec_entry=exec_va+exec_headers[0].oh.entry_rva;
    }
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    if(user_requested) {
        if(!ntos_fault::choose_virtual(0x00710000u,1u,
                                      reservations,count,&ps_user_va)) {
            *err="no private P0 user-stack VA";goto done;
        }
        ps_user_raw=static_cast<uint8_t *>(Mem_Malloc(4096u+4095u));
        if(!ps_user_raw) {
            *err="no physical P1 user stack";goto done;
        }
        auto *aligned=static_cast<uint8_t *>(align_page(ps_user_raw));
        Mem_Memset(aligned,0,4096u);
        g_ps_user.va=ps_user_va;
        g_ps_user.physical=mmu::phys_addr(aligned);
        g_ps_user.backing=aligned;
    }
#endif
#if NTOS_ENABLE_CHILD_PS_RESOURCES
    if(
#if NTOS_ENABLE_PE_PAIR
       twin_requested || kill_requested
#else
       false
#endif
    ) {
        void *mem=Mem_Malloc(sizeof(nt::Process));
        if(!mem) {
            *err="cannot reserve child Ps-owned resource table";goto done;
        }
        g_ps_child_process=::new(mem) nt::Process{};
        g_ps_child_process->pool.configure(
            nullptr,&nt_host_alloc,&nt_host_free);
        if(!g_ps_child_process->begin()) {
            *err="child Ps resource object refused start";goto done;
        }
    }
#endif
    for(unsigned i=0u;i<count;++i) {
        if(!ntos_rebase::apply(nodes[i].hdr,nodes[i].mapped.image,
                               nodes[i].mapped.vsize,nodes[i].mapped.base,err))
            goto done;
        exports[i]={i==0u ? "" : nodes[i].name,
                    &nodes[i].hdr,&nodes[i].mapped};
    }
#if NTOS_ENABLE_PE_PAIR
    if(second_exe.raw) {
        // The PE is independent of parent imports/DLL_PROCESS_ATTACH.
        // Co-resident mappings have DISJOINT VA ranges, same KPROCESS.
        uint32_t chosen=0u;
        if(count>=NTOS_MAX_MODULES ||
           !ntos_rebase::choose_base(0x00500000u,
                second_exe.npages,reservations,count,&chosen) ||
           !ntos_rebase::apply(second_hdr,second_exe.image,
                second_exe.vsize,chosen,err)) {
            *err="cannot rebase second import-free PE";
            goto done;
        }
        second_exe.base=chosen;
        second_entry=chosen+second_hdr.oh.entry_rva;
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
        if(root_requested &&
           ntos_rebase::overlap(second_exe.base,second_exe.npages,
                               psroot_va,1u)) {
            *err="second EXE overlaps P0 root canary";goto done;
        }
#endif
    }
#endif
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
#if NTOS_NO_LOADER_PROGRESS_LOG
        (void)total;
#else
        char msg[80], *p=msg;
        log_text(p,"Linked "); log_number(p,static_cast<uint32_t>(total),10);
        log_text(p," import(s), "); log_number(p,count-1u,10);
        log_text(p," DLL(s)"); *p=0; ntos_puts(msg);
#endif
    }
    api_raw=static_cast<uint8_t *>(Mem_Malloc(0x4000u+0xFFFu));
    if(!api_raw){*err="API mapping allocation failed";goto done;}
    api_block=static_cast<uint8_t *>(align_page(api_raw));
    Mem_Memset(api_block,0,0x4000u);
    *reinterpret_cast<void **>(api_block)=&g_ntos_api;
    {
        auto *api_meta=reinterpret_cast<uint32_t *>(api_block);
        api_meta[1]=NTOS_API_MAGIC;
        api_meta[2]=NTOS_API_VERSION;
        api_meta[3]=static_cast<uint32_t>(sizeof(g_ntos_api)/sizeof(uint32_t));
    }

    for(unsigned i=0u;i<count;++i)
        flush_image(nodes[i].mapped.image,nodes[i].mapped.vsize);
#if NTOS_ENABLE_PE_PAIR
    if(second_exe.raw)flush_image(second_exe.image,second_exe.vsize);
#endif
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(root_requested)flush_image(g_ps_root.backing[0],8192u);
#endif
#if NTOS_ENABLE_PS_EXEC
    if(exec_requested)
        for(unsigned i=0u;i<2u;++i)
            flush_image(exec_images[i].image,
                        exec_images[i].npages*4096u);
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    if(user_requested)flush_image(g_ps_user.backing,4096u);
#endif
    flush_image(api_block,0x4000u);

#if NTOS_ENABLE_NT_PRIVATE_VM
    // Allocate once in restored P1 host world, before guest VBR ownership.
    // Neither NtCommit nor the VBR fault callback can allocate SDK memory.
    vm_raw=static_cast<uint8_t *>(Mem_Malloc(64u*4096u+0xFFFu));
    if(!vm_raw){*err="cannot reserve private P1 VM frame arena";goto done;}
    {
        uint8_t *vm_frames=static_cast<uint8_t *>(align_page(vm_raw));
        g_nt_process.vm.configure(vm_frames,mmu::phys_addr(vm_frames));
    }
#endif
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
                                    nodes[i].mapped.npages,
                                    &nodes[i].hdr)) {
                *err="cannot register virtual DLL backing";goto done;
            }
#if NTOS_ENABLE_PE_PAIR
        if(second_exe.raw &&
           !ntos_hw::add_module(second_exe.base,second_exe.image,
                                second_exe.npages,&second_hdr)) {
            *err="cannot register resident second executable";goto done;
        }
#endif
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
        if(root_requested) {
            auto &pager=ntos_hw::g_context.table;
            g_ps_root.module_index=pager.modules_used;
            if(!pager.add(psroot_va,g_ps_root.pa[0],1u,nullptr,false)) {
                *err="cannot register isolated P0 data root";goto done;
            }
        }
#endif
#if NTOS_ENABLE_PS_EXEC
        if(exec_requested) {
            auto &pager=ntos_hw::g_context.table;
            g_ps_root.exec_module_index=pager.modules_used;
            if(!pager.add(exec_va,g_ps_root.exec_pa[0],
                          g_ps_root.exec_pages,&exec_headers[0],true)) {
                *err="cannot register private executable root";goto done;
            }
        }
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
        if(user_requested && !ntos_hw::g_context.table.add(
                ps_user_va,g_ps_user.physical,1u,nullptr,false)) {
            *err="cannot register unprivileged user stack";goto done;
        }
#endif
#if NTOS_ENABLE_NT_PRIVATE_VM
        ntos_hw::g_context.table.set_extra(
            &g_nt_process.space,[](void *owner,uint32_t va,uint32_t *pa,
                                ntos_mm::Access *rights) {
                return static_cast<nt::AddressSpaceRoot *>(owner)->
                    resolve(va,pa,rights);
            });
#endif
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
#if NTOS_ENABLE_PE_PAIR
            // Separate PE .data/.bss must also be resident, not just its
            // entry instruction. Eager budget includes these pages.
            if(second_exe.raw)
                for(uint32_t p=0u;p<second_exe.npages;++p)
                    if(!ntos_hw::prime(second_exe.base+p*0x1000u,true)) {
                        *err="cannot prime secondary executable data";
                        goto done;
                    }
#endif
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
#if NTOS_ENABLE_PE_PAIR
        if(second_exe.raw &&
           (!ntos_hw::prime(second_exe.base,true) ||
            !ntos_hw::prime(second_entry,true))) {
            *err="no resident second executable entry";goto done;
        }
#endif
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
        if(root_requested && !ntos_hw::prime(psroot_va,true)) {
            *err="cannot pin isolated P0 root data page";goto done;
        }
#endif
#if NTOS_ENABLE_PS_EXEC
        if(exec_requested)
            for(uint32_t i=0u;i<g_ps_root.exec_pages;++i)
                if(!ntos_hw::prime(exec_va+i*4096u,true)) {
                    *err="cannot pin private executable pages";goto done;
                }
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
        if(user_requested && !ntos_hw::prime(ps_user_va,true)) {
            *err="cannot pin user-mode P0 stack";goto done;
        }
#endif
        // Never call Hollyhock Debug_Printf/LCD_Refresh while our
        // experimental VBR+0x400 exception handler owns VBR. SDK output
        // can temporarily switch MMU/interrupt worlds behind our back.
        if(!irq_owner.acquire()) {
            *err="VBR exception ownership install failed";goto done;
        }
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
        if(root_requested)g_ps_root.ready=true;
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
        if(user_requested)g_ps_user.ready=true;
#endif
    }
#else
    {
        // A SH7305 UTLB is fully associative: map at fixed slot numbers,
        // exactly as CP400 OS_02.01.7002 mmu_replace_entry_4k() does.
        uint32_t slot=0u;
        for(unsigned i=0u;i<count;++i) {
            const MappedImage &m=nodes[i].mapped;
            for(uint32_t p=0u;p<m.npages;++p,++slot)
                mmu::map_page_at_index_flags(
                    m.base+p*0x1000u,
                    mmu::phys_addr(m.image+p*0x1000u),
                    slot,
#if NTOS_ENFORCE_PE_PAGE_PERMISSIONS
                    ntos_mm::pe_page_access(nodes[i].hdr,p)==
                            ntos_mm::Access::read_only ?
                        mmu::PTEL_PAGE_READ_ONLY : mmu::PTEL_PAGE
#else
                    mmu::PTEL_PAGE
#endif
                );
        }
        for(uint32_t p=0u;p<mmu::API_WINDOW_PAGES;++p)
            mmu::map_page_at_index(
                mmu::VA_API_WINDOW+p*0x1000u,
                mmu::phys_addr(api_block+p*0x1000u),
                mmu::SLOT_API_FIRST+p,0u);
    }
#endif

    mmu::enable_at();
    // Ps creates a KPROCESS plus its first KTHREAD for the loaded
    // foreground PE. DLL_PROCESS_ATTACH and EXE entry share this thread;
    // COREDLL remains a user PE DLL, not an implementation of Ke.
    if(g_nt_process.ke.begin(nodes[0].mapped.base,
                             nodes[0].mapped.vsize,parent_pid)!=nt::SUCCESS ||
       g_nt_process.ke.enter_guest(
           nodes[0].mapped.base+hdr.oh.entry_rva)!=nt::SUCCESS) {
        *err="Ps failed to initialize the foreground KTHREAD";
        goto done;
    }
#if NTOS_ENABLE_NT_PRIVATE_VM
    if(!g_nt_process.space.bind(
          g_nt_process.ke.process.unique_process_id,
          &g_nt_process.ke.process.unique_process_id,
          &g_nt_process.vm)) {
        *err="Ps failed to bind process-private VA root";goto done;
    }
#endif
#if NTOS_ENABLE_TLB_MISS_PAGER && NTOS_ENABLE_KE_USER_SYSCALL
    ntos_hw::g_context.ke=&g_nt_process.ke;
#endif
#if !NTOS_ENABLE_TLB_MISS_PAGER
    for(unsigned i=0u;i<count;++i)
        mmu::cache_flush_invalidate_block(
             reinterpret_cast<void *>(nodes[i].mapped.base),
             nodes[i].mapped.vsize);
    mmu::cache_flush_invalidate_block(
        reinterpret_cast<void *>(mmu::VA_API_WINDOW),0x4000u);
#endif

    // DllMain runs only AFTER the full graph is mapped, relocated and all
    // IAT entries snapped. Initialize dependencies BEFORE their consumers.
    // A rejected PROCESS_ATTACH rolls back EXACTLY the successfully
    // attached DLLs in reverse order; never call EXE entry after failure.
    if(!dll_plan.configure(count)) {
        *err="invalid DLL graph for initialization";goto done;
    }
    for(unsigned i=1u;i<count;++i) {
        if(!dll_plan.entry(i,nodes[i].hdr.oh.entry_rva!=0u)) {
            *err="cannot register DLL entry";goto done;
        }
        const auto &m=nodes[i];
        if(!m.hdr.oh.import_rva)continue;
        bool terminated=false;
        for(uint32_t di=0u;di<m.hdr.oh.import_size/20u;++di) {
            pe::ImportDesc d{};
            if(!pe::read_import_desc(m.hdr,m.mapped.image,
                                     m.mapped.vsize,di,&d)) {
                terminated=true;break;
            }
            char name[40]{};
            if(!pe::rva_string(m.mapped.image,m.mapped.vsize,
                               d.name_rva,name,sizeof(name))) {
                *err="DLL initialization import name corrupted";goto done;
            }
            for(unsigned j=1u;j<count;++j)
                if(i!=j && module_matches(name,nodes[j].name))
                    (void)dll_plan.depends(i,j);
        }
        if(!terminated) {
            *err="DLL initialization dependency graph unterminated";goto done;
        }
    }
    if(!dll_plan.plan()) {
        *err="could not order DLL_PROCESS_ATTACH";goto done;
    }
    {
        // ABI: int DllMain(HINSTANCE module, DWORD reason, void *reserved)
        // where PROCESS_ATTACH=1 and PROCESS_DETACH=0. Only successful
        // attach callbacks are placed on the per-run detach stack.
        using dll_entry=int (*)(uint32_t,uint32_t,void *);
        auto callback=[](void *opaque,unsigned index,bool attach)->bool {
            auto *modules=static_cast<ModuleNode *>(opaque);
            const ModuleNode &m=modules[index];
            if(!m.hdr.oh.entry_rva)return true;
            dll_entry entry=reinterpret_cast<dll_entry>(
                m.mapped.base+m.hdr.oh.entry_rva);
            const int result=entry(m.mapped.base,attach?1u:0u,nullptr);
            return !attach || result!=0;
        };
        app_console_started=false;
#if NTOS_ENABLE_PE_PAIR
        if(second_exe.raw) {
            g_pe_pair.entry=second_entry;
            g_pe_pair.base=second_exe.base;
            g_pe_pair.progress=0u;
            g_pe_pair.ready=true;
        }
#endif
        if(!dll_plan.attach(callback,nodes)) {
            *err="DLL_PROCESS_ATTACH returned FALSE";
            // attach() already detached exactly the accepted dependencies.
            goto done;
        }
        using exe_entry=int (*)(const NtosApi *,char **,char **);
        exe_entry entry=reinterpret_cast<exe_entry>(
            nodes[0].mapped.base+hdr.oh.entry_rva);
        *out_ret=entry(&g_ntos_api,nullptr,nullptr);
        *err="ok";
        rc=0;
        dll_plan.detach(callback,nodes);
    }

done:
#if NTOS_ENABLE_PE_PAIR
    g_pe_pair.ready=false;
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    g_ps_user.ready=false;
    ntos_hw::g_context.user_loop_armed=false;
#endif
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(g_ps_root.ready && mmu::at_enabled() &&
       ntos_hw::g_context.table.lookup(g_ps_root.va)>=0) {
        mmu::cache_writeback_invalidate_block(
            reinterpret_cast<void *>(g_ps_root.va),4096u);
        mmu::cache_writeback_invalidate_block(
            g_ps_root.backing[g_ps_root.active],4096u);
    }
    g_ps_root.ready=false;
#endif
    mmu::disable_at_flush();
    irq_owner.release();
    host_world::restore(host_state);
#if NTOS_ENABLE_DLL_INIT_TRACE
    if(dll_plan.planned) {
        char msg[80], *p=msg;
        log_text(p,"DLL init: attach=");
        log_number(p,dll_plan.detached,10);
        log_text(p," detach=");
        log_number(p,dll_plan.detached,10);
        log_text(p," rejected=");
        log_number(p,dll_plan.failed,10);
        log_text(p," cycles=");
        log_number(p,dll_plan.cycle_breaks,10);
        *p=0;ntos_puts(msg);
    }
#endif
#if NTOS_STREAMED_IMAGE_LOADING && NTOS_PE_STREAM_LOG
    if(source_path) {
        char msg[72], *p=msg;
        log_text(p,"Skipped PE source buffers: ");
        log_number(p,g_skipped_pe_staging_bytes,10);
        log_text(p," bytes");
        *p=0;
        ntos_puts(msg); // host VBR and MMU already restored
    }
#endif
#if NTOS_ENABLE_TLB_MISS_PAGER
    // All host UI work occurs AFTER restoring the original VBR and SR.
    // Report the actual fault/eviction count when emulator testing PAGERTEST.
    if(rc==0 || ntos_hw::g_context.fault_error!=0u) {
        char msg[80], *p=msg;
        log_text(p,"PF="); log_number(p,ntos_hw::g_context.hw_faults,10);
        log_text(p," HINT="); log_number(p,ntos_hw::g_context.hint_calls,10);
        log_text(p," EV="); log_number(p,ntos_hw::g_context.table.evictions,10);
        log_text(p," BAD="); log_number(p,ntos_hw::g_context.fault_error,10);
        *p=0; ntos_puts(msg);
    }
    if(ntos_hw::g_context.hw_faults!=0u &&
       (rc==0 || ntos_hw::g_context.fault_error!=0u)) {
        char msg[80], *p=msg;
        log_text(p,"TEA="); log_number(p,ntos_hw::g_context.last_tea,16,8);
        log_text(p," EV="); log_number(p,ntos_hw::g_context.last_expevt,16,3);
        log_text(p," SPC="); log_number(p,ntos_hw::g_context.last_spc,16,8);
        *p=0; ntos_puts(msg);
        p=msg; log_text(p,"SSR="); log_number(p,ntos_hw::g_context.last_ssr,16,8);
        log_text(p," MMUCR="); log_number(p,ntos_hw::g_context.last_mmucr,16,8);
        *p=0; ntos_puts(msg);
    }
#if NTOS_ENABLE_KE_USER_SYSCALL
    if(g_nt_process.ke.thread.syscall_count ||
       ntos_hw::g_context.service_rejected) {
        char msg[80], *p=msg;
        log_text(p,"KE PID=");log_number(p,g_nt_process.ke.process.unique_process_id,10);
        log_text(p," TID=");log_number(p,g_nt_process.ke.thread.unique_thread_id,10);
        log_text(p," SVC=");log_number(p,g_nt_process.ke.thread.syscall_count,10);
        log_text(p," YIELD=");log_number(p,g_nt_process.ke.thread.voluntary_yields,10);
        log_text(p," BADGATE=");log_number(p,ntos_hw::g_context.service_rejected,10);
        *p=0;ntos_puts(msg);
    }
#endif
#if NTOS_ENABLE_GUEST_PROTECT_DISPATCH
    if(ntos_hw::g_context.general_faults ||
       ntos_hw::g_context.user_probe_returns) {
        char msg[80], *p=msg;
        log_text(p,"PROT=");log_number(p,ntos_hw::g_context.general_faults,10);
        log_text(p," REC=");log_number(p,ntos_hw::g_context.guest_guard.accepted,10);
        log_text(p," UNEXP=");
        log_number(p,ntos_hw::g_context.guest_guard.forwarded,10);
        log_text(p," USRRET=");
        log_number(p,ntos_hw::g_context.user_probe_returns,10);
        *p=0;ntos_puts(msg);
    }
#endif
#if NTOS_ENABLE_CONTROLLED_VBR_FAULT_PROBE
    if(ntos_hw::g_context.probe_seen || ntos_hw::g_context.probe_armed) {
        char msg[72], *p=msg;
        log_text(p,"VBR-CANARY seen=");
        log_number(p,ntos_hw::g_context.probe_seen,10);
        log_text(p," other=");
        log_number(p,ntos_hw::g_context.probe_unexpected,10);
        *p=0;ntos_puts(msg);
    }
#endif
    LCD_Refresh();
#endif
    // Payloads may exit or fail without calling CloseHandle/FindClose.
    // Do not start a second filesystem operation until the leaked SDK
    // descriptor has been closed. Never guess an unknown SDK handle.
    if(!fs_recover()) {
        *err="filesystem close failed; refusing further file operations";
        g_fatal.reason=2;
        rc=-2;
    }
    // Discard every per-process typed handle only after the single SDK
    // descriptor is confirmed closed (or the gate is permanently poisoned).
    const uint32_t leaked_handles=g_nt_process.handles.used;
    const uint32_t leaked_pool=g_nt_process.pool.current;
#if NTOS_ENABLE_GUEST_IO
    g_nt_io.shutdown(g_nt_process.ke.process.unique_process_id);
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(g_ps_root.twin && g_ps_root.child.active)
        g_nt_io.shutdown(g_ps_root.child.unique_process_id);
#endif
#endif
    g_nt_process.finish(); // NEVER calls SDK File_Close a second time
    if(leaked_handles || leaked_pool) {
        char msg[80], *p=msg;
        log_text(p,"NT cleanup: handles=");
        log_number(p,leaked_handles,10);
        log_text(p," ExBytes=");
        log_number(p,leaked_pool,10);
        *p=0;ntos_puts(msg);
    }
#if NTOS_ENABLE_TLB_MISS_PAGER
    if(ntos_hw::g_context.fault_error) {
        g_fatal.tea=ntos_hw::g_context.last_tea;
        g_fatal.expevt=ntos_hw::g_context.last_expevt;
        g_fatal.spc=ntos_hw::g_context.last_spc;
        g_fatal.ssr=ntos_hw::g_context.last_ssr;
        g_fatal.mmucr=ntos_hw::g_context.last_mmucr;
        g_fatal.reason=1;
        *err="unhandled NTOS MMU fault";
        rc=-2;  // only report after MMU/VBR ownership is restored
    }
#endif
    if(vm_raw) Mem_Free(vm_raw);
    if(api_raw) Mem_Free(api_raw);
#if NTOS_ENABLE_PE_PAIR
    if(second_exe.raw)Mem_Free(second_exe.raw);
#endif
#if NTOS_ENABLE_PSROOT && NTOS_ENABLE_TLB_MISS_PAGER
    if(psroot_raw)Mem_Free(psroot_raw);
#endif
#if NTOS_ENABLE_PSUSER && NTOS_ENABLE_TLB_MISS_PAGER
    if(ps_user_raw)Mem_Free(ps_user_raw);
#endif
#if NTOS_ENABLE_CHILD_PS_RESOURCES
    if(g_ps_child_process) {
        if(g_ps_child_process->active)g_ps_child_process->finish();
        g_ps_child_process->~Process();
        Mem_Free(g_ps_child_process);
        g_ps_child_process=nullptr;
    }
#endif
#if NTOS_ENABLE_PS_EXEC
    for(unsigned i=0u;i<2u;++i)
        if(exec_images[i].raw)Mem_Free(exec_images[i].raw);
#endif
    for(unsigned i=0u;i<NTOS_MAX_MODULES;++i) {
        if(nodes[i].mapped.raw) Mem_Free(nodes[i].mapped.raw);
        if(i && nodes[i].file) Mem_Free(nodes[i].file);
    }
    return rc;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
constexpr int kMaxFiles=static_cast<int>(ntos_picker::MAX_FILES);
constexpr int kMaxName=static_cast<int>(ntos_picker::MAX_NAME);
// Lifetime guard for the launcher catalog. With 256 entries, this cannot
// reside on the host C stack or in the HH3 BSS. Every return/continue
// from the picker releases P1 heap memory, even on PE errors.
struct PickerCatalogOwner {
    ntos_picker::Catalog *catalog=nullptr;
    bool allocate() {
        void *raw=Mem_Malloc(sizeof(ntos_picker::Catalog));
        if(!raw)return false;
        catalog=::new(raw) ntos_picker::Catalog{};
        return true;
    }
    ~PickerCatalogOwner() {
        if(catalog) {
            catalog->~Catalog();
            Mem_Free(catalog);
        }
    }
    PickerCatalogOwner(const PickerCatalogOwner &)=delete;
    PickerCatalogOwner &operator=(const PickerCatalogOwner &)=delete;
    PickerCatalogOwner()=default;
};

static char_const16_t g_find_pattern_exe[] = {
    '\\', 'f', 'l', 's', '0', '\\', '*', '.', 'E', 'X', 'E', 0,
};
static char_const16_t g_find_pattern_dll[] = {
    '\\', 'f', 'l', 's', '0', '\\', '*', '.', 'D', 'L', 'L', 0,
};

static void banner(void) {
    fillScreen(color(0, 0, 40));
    Debug_Printf(0, 0, true, 0, "%s", "NTOS 0.3 - SH4BE PE loader + UTLB");
    Debug_Printf(0, 1, false, 0, "%s", "Phone API 1 / 28 gates - select EXE");
    LCD_Refresh();
    cur_row = 3;
}

[[maybe_unused]] static void tiny_utoa(uint32_t v, char *buf) {
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
// Nonfatal PE/ABI/import errors are application failures, not kernel BSODs.
// All mapped pages have already been torn down by run_mapped's done path.
// A *returned* fatal kernel fault is distinct from a bad PE dependency.
// If the VBR handler cannot return, this screen cannot be reached; no
// attempt is made to recover a broken interrupt/stack context in place.
static void kernel_fault_screen(void) {
    (void)fs_recover();  // best effort; no new filesystem operations
    fillScreen(color(2, 6, 16));
    Debug_Printf(0, 0, true, 0, "%s", "NTOS KERNEL FAULT - SAFE EXIT");
    Debug_Printf(0, 2, false, 0, "TEA=%08lX EV=%03lX",
                 static_cast<unsigned long>(g_fatal.tea),
                 static_cast<unsigned long>(g_fatal.expevt));
    Debug_Printf(0, 3, false, 0, "SPC=%08lX SSR=%08lX",
                 static_cast<unsigned long>(g_fatal.spc),
                 static_cast<unsigned long>(g_fatal.ssr));
    Debug_Printf(0, 4, false, 0, "MMUCR=%08lX",static_cast<unsigned long>(g_fatal.mmucr));
    Debug_Printf(0, 6, false, 0, "%s", "No further disk access. Quit NTOS.");
    Debug_Printf(0, 8, false, 0, "%s", "EXE / CLEAR: exit to launcher");
    LCD_Refresh();
    uint32_t k1=0u,k2=0u;
    do {getKey(&k1,&k2);} while((k1|k2)!=0u);
    do {
        getKey(&k1,&k2);
    } while(!testKey(k1,k2,KEY_EXE) &&
            !testKey(k1,k2,KEY_CLEAR) &&
            !testKey(k1,k2,KEY_LEFT));
}
static void app_load_error(const char *reason) {
    fillScreen(color(29, 30, 31));
    Debug_Printf(0, 1, true, 0, "%s", "NTOS - app could not start");
    Debug_Printf(0, 3, false, 0, "%s", reason ? reason : "unknown application error");
    Debug_Printf(0, 5, false, 0, "%s", "Kernel and filesystem remain active");
    Debug_Printf(0, 7, false, 0, "%s", "Any key: return to app picker");
    LCD_Refresh();
}
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

// Exit to Hollyhock only after attempting to release the last NTOS-owned
// descriptor, including the no-files and user-CLEAR picker paths.
static int safe_host_exit(int status) {
    const bool recovered=fs_recover();
#if NTOS_PROCESS_HEAP_RESIDENT
    // The caller must have exited every P0 image and restored HH3
    // VBR/MMU before releasing the heap-backed Process.
    if(g_nt_process_storage &&
       !mmu::at_enabled() && !g_nt_process.active) {
        g_nt_process_storage->~Process();
        Mem_Free(g_nt_process_storage);
        g_nt_process_storage=nullptr;
    }
#endif
    return recovered ? status : -1;
}
int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    while (true) {
    g_fatal=FatalSnapshot{};
    // A failed close is an interlock, not a reason to probe more files.
    if(!fs_recover()) {
        ntos_puts("FS locked: SDK handle did not close");
        ntos_puts("Stop NTOS - do not retry files");
        LCD_Refresh();
        return safe_host_exit(-1);
    }
    banner();

    // Scan flash for payload images (*.EXE) and modules (*.DLL). NTOS maps
    // a COREDLL.DLL / COREDLL.EXE as the shared module every EXE imports
    // from; it is not shown in the picker. File_FindFirst returns UCS-16
    // names; File_Open takes ANSI paths, so convert on the way in.
    PickerCatalogOwner catalog_owner{};
    if(!catalog_owner.allocate()) {
        ntos_puts("EXE catalog P1 allocation failed");
        LCD_Refresh();
        return safe_host_exit(-1);
    }
    auto &names=catalog_owner.catalog->names;
    auto &sizes=catalog_owner.catalog->sizes;
    // HH3's physical key enum layout is part of our bitfield contract.
    static_assert(KEY_3==0x00000008u && KEY_6==0x00000010u &&
                  KEY_9==0x00000020u && KEY_BACKSPACE==0x00000080u &&
                  KEY_EXE==0x04000000u && KEY_CLEAR==0x00020000u);
    static_assert(KEY_0==0x04000000u && KEY_1==0x08000000u &&
                  KEY_2==0x00080000u && KEY_4==0x10000000u &&
                  KEY_5==0x00100000u && KEY_7==0x20000000u &&
                  KEY_8==0x00200000u && KEY_UP==0x00800000u &&
                  KEY_DOWN==0x00400000u);
    int count = 0;

    for (const char_const16_t *pattern :
         {g_find_pattern_exe, g_find_pattern_dll}) {
        int find = 0;
        char_const16_t name[32] = {};
        File_FindInfo info{};
        if (fs_find_first(pattern, &find, name, &info) == 0) {
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
                if (fs_find_next(find, name, &info) != 0)
                    break;
            } while (true);
            fs_find_close(find);
        }
    }

    // Build the pickable list (everything except COREDLL.*) and find the
    // coredll module for the loader.
    auto &pick=catalog_owner.catalog->pick;
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
        return safe_host_exit(0);
    }

    // The LCD displays 29 rows of a 256-entry heap-backed catalog.
    // UP/DOWN scroll across every entry, with 1..256 numeric jump.
    auto draw_picker=[&](const ntos_picker::Picker &ui) {
        Debug_Printf(0,2,true,0,"Selected %u/%u   Jump:%s%u%s       ",
            ui.selected+1u,ui.count,
            ui.typed_digits ? "" : "-",
            ui.typed_digits ? ui.typed : 0u,
            ui.typed_digits && (!ui.typed || ui.typed>ui.count) ? " (range)" : "");
        Debug_Printf(0,3,false,0,
            "%s","EXE run | 0-9 jump | BACK edit | UP/DOWN");
        for(unsigned row=0u;row<ntos_picker::VISIBLE;++row) {
            char line[41];
            const unsigned ordinal=ui.first+row;
            if(ordinal<ui.count)
                ntos_picker::format_row(
                    line,ordinal,ui.selected,names[pick[ordinal]],
                    sizes[pick[ordinal]]);
            else {
                for(unsigned c=0u;c<40u;++c)line[c]=' ';
                line[40]=0;
            }
            Debug_Printf(0,row+4u,false,0,"%s",line);
        }
        LCD_Refresh();
    };
    ntos_picker::Picker picker{};
    picker.init(static_cast<unsigned>(count));
    draw_picker(picker);
    // Do not reuse the EXE press that dismissed a previous payload/error.
    uint32_t key1=0u,key2=0u;
    do{getKey(&key1,&key2);}while((key1|key2)!=0u);
    bool quit=false;
    while(!quit) {
        key1=0u;key2=0u;
        getKey(&key1,&key2);
        const auto action=picker.key(key1,key2);
        if(action==ntos_picker::launch)quit=true;
        else if(action==ntos_picker::exit_host)
            return safe_host_exit(0);
        else if(action==ntos_picker::redraw)
            draw_picker(picker);
    }
    const int selected=static_cast<int>(picker.selected);

    // Open the selected PE. Streaming mode only reads its header here;
    // copy-load comparison mode retains the former full-file staging path.
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

    uint8_t *file=nullptr;
    uint32_t fsize=0u;
    pe::Image hdr{};
    const char *mapped_path=nullptr;
#if NTOS_STREAMED_IMAGE_LOADING
    const char *load_error=nullptr;
    if(!read_pe_source(path,false,false,&hdr,nullptr,&fsize,&load_error)) {
        app_load_error(load_error);
        wait_error_key_release();
        continue;
    }
    // Copy-load fallback still needs a staged buffer; normal mapped
    // execution no longer allocates it at all.
    if(mmu::at_enabled()) {
        uint32_t staged_bytes=0u;
        if(!read_module_file(path,&file,&staged_bytes,&load_error) ||
           staged_bytes!=fsize) {
            if(file)Mem_Free(file);
            app_load_error(staged_bytes && staged_bytes!=fsize ?
                           "PE file changed during load" :
                           (load_error?load_error:"Failed to stage copy-load PE"));
            wait_error_key_release();
            continue;
        }
    } else mapped_path=path;
#else
    int fd = fs_open(path, FILE_OPEN_READ);
    if (fd < 0) {
        app_load_error("Could not open the selected EXE (file busy?)");
        wait_error_key_release();
        continue;
    }
    fsize = sizes[pick[selected]];
    file = static_cast<uint8_t *>(Mem_Malloc(fsize != 0 ? fsize : 1));
    if (file == nullptr) {
        app_load_error("Not enough memory to stage EXE");
        fs_close(fd);
        wait_error_key_release();
        continue;
    }
    if (File_Read(fd, file, static_cast<int>(fsize)) !=
        static_cast<int>(fsize)) {
        app_load_error("Could not read complete EXE");
        Mem_Free(file);
        fs_close(fd);
        wait_error_key_release();
        continue;
    }
    fs_close(fd);

    // Parse the header once to pick the load path.
    if (pe::parse(file, fsize, &hdr) != 0) {
        app_load_error("Invalid SH4BE PE executable");
        Mem_Free(file);
        wait_error_key_release();
        continue;
    }


#endif

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
        const int mapped_rc=run_mapped(file,fsize,hdr,core_path,core_size,
                       extra_path,extra_size,&ret,&err,mapped_path);
        if(mapped_rc != 0) {
            if(mapped_rc==-2) {
                if(file)Mem_Free(file);
                kernel_fault_screen();
                return safe_host_exit(-1);
            }
            app_load_error(err);
            LCD_Refresh();
            if(file)Mem_Free(file);
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
            app_load_error(err);
            LCD_Refresh();
            Mem_Free(file);
            wait_error_key_release();
        continue;
        }

        char loaded_msg[64], *loaded_p=loaded_msg;
        log_text(loaded_p,"Loaded at 0x");
        log_number(loaded_p,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image)),16,8);
        log_text(loaded_p," entry 0x");
        log_number(loaded_p,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image)) + hdr.oh.entry_rva,16,8);
        *loaded_p=0; ntos_puts(loaded_msg);

        // Entry contract: r4 = &NtosApi, r5 = argv, r6 = envp.
        using entry_fn = int (*)(const NtosApi *, char **, char **);
        entry_fn entry = reinterpret_cast<entry_fn>(
            reinterpret_cast<uint32_t>(image) + hdr.oh.entry_rva);
        if(!nt_process_configure() || !g_nt_process.begin()) {
            app_load_error("NT process resources still held");
            Mem_Free(image);
            if(file)Mem_Free(file);
            wait_error_key_release();
            continue;
        }
        ret = entry(&g_ntos_api, nullptr, nullptr);
        if(!fs_recover()) {
            g_nt_process.finish();
            Mem_Free(image);
            if(file)Mem_Free(file);
            kernel_fault_screen();
            return safe_host_exit(-1);
        }
        g_nt_process.finish();
        Mem_Free(image);
    }

    if(file)Mem_Free(file);
    if(!fs_recover()) {
        ntos_puts("FS locked after application; stopping");
        LCD_Refresh();
        return safe_host_exit(-1);
    }
    // Tear down each image before starting the next: one app resident at a time.
    if(pending_app[0]) {
        for (;;) {
            char next[96];unsigned i=0;
            for(;i+1<sizeof(next)&&pending_app[i];++i)next[i]=pending_app[i];
            next[i]=0;pending_app[0]=0;
            const uint32_t parent_for_next=pending_parent_pid;
            pending_parent_pid=0u;
            uint8_t *app_file=nullptr;uint32_t app_size=0;const char *launch_error=nullptr;
            pe::Image app_hdr{};
#if NTOS_STREAMED_IMAGE_LOADING
            bool ok=read_pe_source(next,false,false,&app_hdr,nullptr,
                                   &app_size,&launch_error);
            if(ok)ok=run_mapped(nullptr,app_size,app_hdr,nullptr,0,nullptr,0,
                                &ret,&launch_error,next,parent_for_next)==0;
#else
            bool ok=read_module_file(next,&app_file,&app_size,&launch_error);
            if(ok)ok=pe::parse(app_file,app_size,&app_hdr)==0;
            if(ok)ok=run_mapped(app_file,app_size,app_hdr,nullptr,0,nullptr,0,&ret,&launch_error,nullptr,parent_for_next)==0;
#endif
            if(app_file)Mem_Free(app_file);
            if(ok && ret) {
                char msg[48], *p=msg;
                log_text(p,"App returned 0x"); log_number(p,static_cast<uint32_t>(ret),16,8);
                *p=0; ntos_puts(msg);
            }
            if(!fs_recover()) {
                ntos_puts("SDK close failed; stop launching apps");
                LCD_Refresh();
                return safe_host_exit(-1);
            }
            if(g_fatal.reason) {
                kernel_fault_screen();
                return safe_host_exit(-1);
            }
            if(!ok){
                app_load_error(launch_error?launch_error:"Invalid application");
                wait_error_key_release();
            }
            if(!pending_app[0]) {
                // A stub/Settings/Calculator exit returns to Start only
                // if that optional phone shell EXE is installed. Completed
                // PROCTEST chains must return cleanly to the picker instead
                // of producing a misleading missing-PE error.
                const bool queue_home=ntos_launch_policy::queue_optional_home(
                    next,[](const char *home) {
                        // Called in restored host context after SDK close.
                        // File_Stat is handle-free and never runs when the
                        // single-owner gate is busy or poisoned.
                        if(g_fs.busy() || g_fs.poisoned)return false;
                        struct File_Stat st{};
                        return File_Stat(home,&st)==0;
                    });
                if(!queue_home)break;
                const char *home=ntos_launch_policy::HOME;
                unsigned j=0;for(;home[j];++j)pending_app[j]=home[j];pending_app[j]=0;
            }
        }
    }
    char result_line[] = "Payload returned 0x00000000";
    const char *digits = "0123456789ABCDEF";
    for(unsigned i=0;i<8;++i)result_line[19+i]=digits[(static_cast<uint32_t>(ret)>>(28-4*i))&15];
    ntos_puts(result_line);
    ntos_puts("EXE: pick again   CLEAR: exit");
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
            if ((testKey(key1, key2, KEY_CLEAR) || testKey(key1, key2, KEY_LEFT)))
                return safe_host_exit(0);
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
