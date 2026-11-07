/* coredll.c -- COREDLL for NTOS, big-endian SH4 (machine 0x01A8).
 *
 * Modeled on WinCE's coredll.dll: the single module every EXE imports its
 * base services from. NTOS copy-loads it into the heap, UTLB-maps its
 * 4 KiB pages at the preferred base 0x00800000 and resolves payload
 * imports against the PE export directory elf2ntx.py builds.
 *
 * Exports (tools/coredll_exports.txt, parsed from WinCE 4.2
 * coredll.def, ~1900 names): a handful are really implemented below,
 * everything else shares one `coredll_stub` (returns 0) so programs can
 * be compiled against the full WinCE API surface and get implementations
 * filled in one by one.
 *
 * The real ones reach NTOS services the way an OS330 C1A add-in reaches
 * the Casio OS: through the API window at VA 0x08100000, whose first
 * word points at the syscall table (NTOS rebuilds that window in UTLB
 * slots 0x37..0x3A before the payload runs).
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

/* ---------------------------------------------------------------------------
 * Really-implemented WinCE APIs
 * ------------------------------------------------------------------------- */

/* WinCE MEMORYSTATUS (GlobalMemoryStatus) */
typedef struct {
    u32 dwLength;
    u32 dwMemLoad;
    u32 dwTotalPhys;
    u32 dwAvailPhys;
    u32 dwTotalPageFile;
    u32 dwAvailPageFile;
    u32 dwTotalVirtual;
    u32 dwAvailVirtual;
} MEMSTATUS;

/* NKDbgPrintfW: debug output. Real CE takes a wide-char format string;
 * we treat the first argument as a narrow string and print it verbatim
 * (argument formatting is not supported -- callers in NTOS payloads pass
 * plain literal lines). */
void NKDbgPrintfW(const char *s)
{
    static u32 row = 0;
    const void **api = ntapi();
    if (api != 0 && api[1] != 0) {
        /* Debug_Printf = the small font (api[0]/puts uses the large one) */
        typedef void (*printf_fn)(u32, u32, u32, u32, const char *, ...);
        ((printf_fn)api[1])(0, row, 0, 0, "%s", s);
        coredll_refresh();
        if (++row > 22)
            row = 0;
    }
}

/* Kernel-mode toggles: NTOS payloads run privileged already, so both are
 * acknowledgements that preserve the original return contracts. */
u32 SetKMode(u32 enable)
{
    (void)enable;
    return 1; /* previous mode: kernel */
}

u32 SetProcPermissions(u32 newperms)
{
    (void)newperms;
    return 0xFFFFFFFFu; /* previous permissions: everything */
}

void *LocalAlloc(u32 flags, u32 bytes)
{
    (void)flags; /* LMEM_FIXED semantics only */
    return coredll_malloc(bytes);
}

u32 LocalFree(void *p)
{
    coredll_free(p);
    return 0; /* NO_ERROR */
}

void *VirtualAlloc(u32 base, u32 size, u32 type, u32 protection)
{
    (void)base;
    (void)type;
    (void)protection; /* MEM_COMMIT + PAGE_READWRITE semantics only */
    return coredll_malloc(size);
}

u32 VirtualFree(void *base, u32 size, u32 freetype)
{
    (void)size;
    (void)freetype;
    coredll_free((void *)base);
    return 1;
}

/* GlobalMemoryStatus with *real* numbers: total = the known HH3 heap
 * size, avail = the largest contiguous block we can actually obtain by
 * descending probe (which doubles as the kernel-memory exercise). */
void GlobalMemoryStatus(MEMSTATUS *ms)
{
    const u32 total = 12u * 1024u * 1024u;
    u32 avail = 0;
    u32 lo = 0x1000, hi = total;
    if (ms == 0)
        return;
    /* binary-probe the largest successful malloc */
    while (lo <= hi) {
        const u32 mid = (lo + hi) / 2u;
        void *p = coredll_malloc(mid);
        if (p != 0) {
            coredll_free(p);
            avail = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    ms->dwLength = sizeof(MEMSTATUS);
    ms->dwMemLoad = 100 - (u32)((unsigned long long)avail * 100 / total);
    ms->dwTotalPhys = total;
    ms->dwAvailPhys = avail;
    ms->dwTotalPageFile = total;
    ms->dwAvailPageFile = avail;
    ms->dwTotalVirtual = total;
    ms->dwAvailVirtual = avail;
}

/* ---------------------------------------------------------------------------
 * Stub fallback: every remaining export in tools/coredll_exports.txt is
 * redirected here by the packer (--exports-file + --stub-symbol).
 * ------------------------------------------------------------------------- */
int coredll_stub(void)
{
    return 0;
}

/* Entry point -- real DLLs have no meaningful one; NTOS never calls it. */
int main(void)
{
    return 0;
}
