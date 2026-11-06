/* coredll.c -- stub COREDLL for NTOS, big-endian SH4 (machine 0x01A8).
 *
 * This is the NTOS equivalent of a WinCE coredll.dll: a single module that
 * every EXE imports its base services from. NTOS copy-loads it into the
 * heap, UTLB-maps its 4 KiB pages at the preferred base 0x00800000 and
 * resolves payload imports against the PE export directory elf2ntx.py
 * builds from the --exports list.
 *
 * The stubs reach NTOS services the same way an OS330 C1A add-in reaches
 * the Casio OS: through the API window at VA 0x08100000, whose first word
 * points at the syscall table (NTOS rebuilds that window in UTLB slots
 * 0x37..0x3A before the payload runs). No relocations are needed: the
 * image is /FIXED at its preferred base and the window address is an
 * absolute constant.
 */
typedef unsigned int u32;

/* OS330 C1A API window contract: *(0x08100000) = syscall table pointer. */
#define API_WINDOW ((const void *const *)0x08100000u)

/* NtosApi field order == table order:
 *   [0] puts(s) [1] printf [2] refresh [3] malloc(n) [4] free(p)
 *   [5] get_key()
 */
static const void **ntapi(void)
{
    const void *tbl = *API_WINDOW;
    return (const void **)(tbl != 0 ? tbl : 0);
}

void coredll_puts(const char *s)
{
    const void **api = ntapi();
    if (api != 0 && api[0] != 0)
        ((void (*)(const char *))api[0])(s);
}

u32 coredll_getkey(void)
{
    const void **api = ntapi();
    if (api != 0 && api[5] != 0)
        return ((u32 (*)(void))api[5])();
    return 0;
}

void coredll_refresh(void)
{
    const void **api = ntapi();
    if (api != 0 && api[2] != 0)
        ((void (*)(void))api[2])();
}

void *coredll_malloc(u32 n)
{
    const void **api = ntapi();
    if (api != 0 && api[3] != 0)
        return ((void *(*)(u32))api[3])(n);
    return 0;
}

void coredll_free(void *p)
{
    const void **api = ntapi();
    if (api != 0 && api[4] != 0)
        ((void (*)(void *))api[4])(p);
}

/* Entry point -- real DLLs have no meaningful one; NTOS never calls it
 * (AddressOfEntryPoint is 0/dummy in the packed image). */
int main(void)
{
    return 0;
}
