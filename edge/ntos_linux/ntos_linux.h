/* ntos_linux.h — NTOS_SH4_linux: Wine-like host env for NTOS payload PEs.
 *
 * Runs x86-64 PE payload DLLs (built with zig cc -target x86_64-windows-gnu)
 * natively on Linux: maps sections, patches the NTOS-style __imp_* IAT slots
 * from builtin COREDLL + other loaded modules, pumps the CE-style message
 * loop, renders the 320x528 device LCD to PNG screenshots and replays
 * scripted input for e2e tests. NOT a SuperH emulator.
 */
#ifndef NTOS_LINUX_H
#define NTOS_LINUX_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t U32;
typedef uint16_t W16;
typedef uint64_t U64;

typedef struct {
    int left, top, right, bottom;
} Rect;

typedef struct {
    int x, y;
} Pt;

#define NTOS_SCREEN_W 320
#define NTOS_SCREEN_H 528

/* WinCE-style window messages used by the payloads (phone.h values). */
#define WM_PAINT_ 15u
#define WM_KEY_ 0x100u
#define WM_CHAR_ 0x102u
#define WM_MOVE_ 0x200u
#define WM_DOWN_ 0x201u
#define WM_UP_ 0x202u
#define WM_QUIT_ 0x12u
#define WM_CREATE_ 1u
#define WM_DESTROY_ 2u

#define MSABI __attribute__((ms_abi))

/* ---------------- PE loader ---------------- */
typedef struct PeModule {
    char name[64];          /* "edge.dll" */
    uint8_t *base;          /* mapping address */
    size_t size;            /* total image size */
    uint32_t entry_rva;
    int is_dll;
    /* export directory (resolved) */
    uint32_t exp_rva_min, exp_rva_max;
    const U32 *exp_funcs;   /* RVA array */
    const U32 *exp_names;   /* RVA array of name RVAs */
    const uint16_t *exp_ords;
    uint32_t exp_count, exp_names_count, exp_ordinal_base;
    const char *exp_dll_name;
    struct PeModule *next;
} PeModule;

/* Load a PE32/PE32+ image (memory-mapped at preferred base when possible,
 * applying base relocations otherwise). Returns 0 on success. */
int pe_load(PeModule *mod, const char *name, const uint8_t *data, size_t size);

/* Call the module entry point (DllMain/DLL_PROCESS_ATTACH). Returns its BOOL. */
int pe_call_entry(PeModule *mod, void *instance);

/* Resolve an export by name; returns NULL if absent. */
void *pe_resolve(PeModule *mod, const char *name);

/* Patch every exported __imp_<name> slot of mod from builtins + other mods. */
int pe_patch_iat_slots(PeModule *mod, PeModule * const *others, size_t other_count);

/* ---------------- host core (built-in COREDLL) ---------------- */
/* All builtin functions use the Win64 ABI (MSABI) so PE code can call them
 * through the patched IAT slots. */

typedef struct HostBrush {
    U32 color;
} HostBrush;

typedef MSABI int (*WndProc)(void *, U32, U32, U32);

typedef struct HostMsg {
    void *hwnd;
    U32 message, wparam, lparam;
    U32 time;
    Pt pt;
    struct HostMsg *next;
} HostMsg;

typedef struct HostPaint {
    void *hdc;
    int erase;
    Rect area;
    int restore, inc;
    unsigned char reserved[32];
} HostPaint;

typedef struct HostSipInfo {
    U32 size, flags;
    Rect desktop, sip;
    U32 data_size;
    void *data;
} HostSipInfo;

typedef struct HostWndClass {
    U32 style;
    WndProc proc;
    int cls_extra, win_extra;
    void *instance, *icon, *cursor, *background;
    const W16 *menu, *name;
} HostWndClass;

void host_init(int argc, char **argv, const char *fs_root);
void host_shutdown(void);

/* framebuffer (0x00BBGGRR device COLORREF-style pixels, like RGB_ macro) */
extern U32 *host_fb;
int host_fb_dirty(void);
void host_save_png(const char *path);
void host_save_ppm(const char *path);

/* scripting helpers used by main.c */
void host_post(U32 message, U32 wp, U32 lp);
void host_request_quit(void);
/* Idle hook: called by get_message_w whenever the app's queue is empty.
 * Return 1 if the hook queued more work (or changed host state), 0 to let
 * the message loop quit. This is how the e2e script drives the app lazily,
 * step by step, interleaved with paints. */
void host_set_idle_hook(int (*hook)(void));
/* SIP helpers for scripts (payloads call SipShowIM/SipGetInfo through IAT). */
void host_sip_set(int visible);
int host_sip_visible(void);

/* debug log */
void host_log(const char *fmt, ...);

/* builtin function registration (name, ms_abi address) */
typedef struct {
    const char *name;
    void *fn;
} BuiltinExport;
extern const BuiltinExport host_builtins[];
extern const size_t host_builtin_count;
void *host_lookup_builtin(const char *name);

#endif
