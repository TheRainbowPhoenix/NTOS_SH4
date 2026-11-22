/* Hardware demand-paging test DLL: 80 distinct 4KiB BSS pages.
 * Gated fault build maps only hot pages, then exercises 80 faults and
 * verifies data survives UTLB eviction. No FPU, no CRT, no SDK in ISR. */
#include <stdint.h>
static volatile uint8_t pages[80u * 4096u];

/* Diagnostic-only NTOS page service [23], deliberately NOT part of
 * COREDLL/WinCE. ClassPad emulators may report an unmapped P0 access as
 * a fatal protection error instead of dispatching a real VBR+0x400 miss.
 * Hinting before each access lets us distinguish emulator exception
 * delivery from actual MMU/TLB installation bugs.
 *
 * The default eager NTOS responds -1 (never call this DLL in that mode).
 * Compile with PAGER_USE_HINTS=0 to exercise the true exception path.
 */
#ifndef PAGER_USE_HINTS
#define PAGER_USE_HINTS 1
#endif
#ifdef NTOS_EXWORD_P1_VM
/* The firmware P0 window 0x08100000 belongs to ClassPad only.
 * EX-word guests receive their host API explicitly, through their EXE
 * entry point, and pass it to this DLL without any P0 dereference. */
static const void *const *g_exword_api;
#endif
static int hint_page(const volatile uint8_t *p)
{
#if PAGER_USE_HINTS
#ifdef NTOS_EXWORD_P1_VM
    const void *const *api=g_exword_api;
#else
    const void *const *const *api_window = (const void *const *const *)0x08100000u;
    const void *const *api = *api_window;
#endif
    if (!api || !api[23])
        return -1;
    return ((int (*)(uint32_t))api[23])((uint32_t)p);
#else
    (void)p;
    return 0;
#endif
}
#ifdef NTOS_EXWORD_P1_VM
uint32_t pager_touch(uint32_t seed,const void *const *api)
#else
uint32_t pager_touch(uint32_t seed)
#endif
{
#ifdef NTOS_EXWORD_P1_VM
    if(!api || !api[23])return 0xBAD10003u;
    g_exword_api=api;
#endif
    uint32_t sum = seed;
    for (uint32_t i = 0; i < 80u; ++i) {
        if (hint_page(&pages[i * 4096u]) != 0)
            return 0xBAD10001u;
        pages[i * 4096u] = (uint8_t)(i + 1u);
        sum += (uint32_t)pages[i * 4096u];
    }
    for (uint32_t i = 0; i < 80u; ++i) {
        if (hint_page(&pages[i * 4096u]) != 0)
            return 0xBAD10002u;
        sum ^= ((uint32_t)pages[i * 4096u]) << ((i & 3u) * 8u);
    }
    return sum;
}
int main(void)
{
    return 1;
}
