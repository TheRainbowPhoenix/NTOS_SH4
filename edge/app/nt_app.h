/* nt_app.h — shared NTOS app boilerplate: IAT imports + WinCE-ish types.
 *
 * Same source builds for:
 *   - device: sh4a_nofpueb-elf-gcc, imports resolved by the NTOS loader
 *     against COREDLL.DLL (payload/ntimport.h model)
 *   - NTOS_SH4_linux host e2e: zig cc -target x86_64-windows-gnu with
 *     -DNTOS_LINUX_BUILD; the host loader patches the exported __imp_ slots
 */
#ifndef NT_APP_H
#define NT_APP_H

#include <stdint.h>

typedef uint32_t U32;
typedef uint16_t W16;

typedef struct {
    int left, top, right, bottom;
} Rect;
typedef struct {
    int x, y;
} Pt;

#define RGB_(r, g, b) ((U32)((r) | ((g) << 8) | ((b) << 16)))
#define WHITE RGB_(255, 255, 255)
#define EDGE_BLUE RGB_(0x00, 0x78, 0xD7)
#define EDGE_BLUE_DARK RGB_(0x00, 0x5A, 0x9E)
#define INK RGB_(29, 42, 58)
#define SURFACE RGB_(242, 247, 251)
#define CARD RGB_(231, 241, 249)
#define MUTED RGB_(101, 120, 138)

/* messages (phone.h numbering) */
#define WM_CREATE_ 1u
#define WM_DESTROY_ 2u
#define WM_PAINT_ 15u
#define WM_KEY_ 0x100u
#define WM_CHAR_ 0x102u
#define WM_MOVE_ 0x200u
#define WM_DOWN_ 0x201u
#define WM_UP_ 0x202u

#ifdef NTOS_LINUX_BUILD
#define NT_SLOT_EXPORT __attribute__((dllexport))
#else
#define NT_SLOT_EXPORT
#endif

#define NT_IMPORT(ret, name, params) \
    typedef ret(*__ntx_fn_t_##name) params; \
    extern NT_SLOT_EXPORT const void *volatile __imp_##name; \
    static __inline __ntx_fn_t_##name __ntx_ref_##name(void) \
    { return (__ntx_fn_t_##name)(uintptr_t)__imp_##name; }
#define NT_DEFINE(name) \
    NT_SLOT_EXPORT const void *volatile __imp_##name __attribute__((section(".idata"))) = (const void *)0;
#define NT_CALL(name, args) __ntx_ref_##name() args

#endif
