/* ntimport.h -- WinCE-style import macros for NTOS payloads.
 *
 * The MSVC/WinCE model: a payload never calls a DLL function directly; it
 * calls through an import address table (IAT) slot that the loader fills
 * in after mapping the DLL ("snapping" the imports). Here each imported
 * function gets a slot variable `__imp_<name>` placed in the .idata
 * section; NTOS maps COREDLL.NTX, looks the name up in its PE export
 * directory and writes the absolute virtual address into the slot.
 *
 * Usage:
 *     #include "ntimport.h"
 *     NT_IMPORT(void, coredll_puts, (const char *s));
 *     NT_IMPORT(uint32_t, coredll_getkey, (void));
 *     ...
 *     NT_DEFINE(coredll_puts)      // one per import, at file scope
 *     NT_DEFINE(coredll_getkey)
 *     ...
 *     NT_CALL(coredll_puts, ("hello"));
 *
 * The slots are `volatile` so the compiler cannot constant-fold reads of
 * the (externally patched) zero-initialized slots.
 */
#pragma once

#include <stdint.h> /* uintptr_t for the import-call cast */
#include "ntabi.h"

/* Host (NTOS_SH4_linux) PE builds must EXPORT the IAT slots so the host
 * loader can find and patch them; device builds keep plain symbols. */
#ifdef NTOS_LINUX_BUILD
#define NT_IAT_EXPORT __attribute__((dllexport))
#else
#define NT_IAT_EXPORT
#endif

/* Declare an imported function: typedef + call helper. */
#define NT_IMPORT(ret, name, params)                                                               \
    typedef ret(*__ntx_fn_t_##name) params;                                                        \
    extern const void *volatile __imp_##name NT_IAT_EXPORT;                                        \
    static __inline __ntx_fn_t_##name __ntx_ref_##name(void)                                       \
    {                                                                                              \
        return (__ntx_fn_t_##name)(uintptr_t)__imp_##name;                                         \
    }

/* Define the IAT slot for an imported function (in .idata, patched by the
 * NTOS loader). */
#define NT_DEFINE(name)                                                                            \
    const void *volatile __imp_##name __attribute__((section(".idata"))) NT_IAT_EXPORT = (const void *)0;

/* Call an imported function: NT_CALL(coredll_puts, ("hi")); */
#define NT_CALL(name, args) __ntx_ref_##name() args

/* Multi-object host builds (several payload .c files linked into ONE DLL,
 * e.g. the Edge app): exactly one object owns each IAT slot definition.
 * Compile the non-owner objects with -DNT_IAT_EXTERN_ONLY so their
 * NT_DEFINE degrades to an extern declaration. */
#ifdef NT_IAT_EXTERN_ONLY
#undef NT_DEFINE
#define NT_DEFINE(name) extern const void *volatile __imp_##name NT_IAT_EXPORT;
#endif

/* Opt-in boundary calling-convention experiment. Both the imported
 * FUNCTION POINTER TYPE and the exported function must agree. Plain
 * NT_IMPORT is intentionally unchanged so native NTOS calls stay stable.
 * This is an SH Renesas ABI candidate, not an asserted MSVC SH4 ABI. */
#define NT_IMPORT_CE_CANDIDATE(ret, name, params)                                                  \
    typedef ret(*__ntx_fn_t_##name) params NT_ABI_CE_CANDIDATE;                                    \
    extern const void *volatile __imp_##name;                                                      \
    static __inline __ntx_fn_t_##name __ntx_ref_##name(void)                                       \
    {                                                                                              \
        return (__ntx_fn_t_##name)(uintptr_t)__imp_##name;                                         \
    }
