/* memtool.c -- SH4 BE recreation of the WinCE MEMTOOL.EXE found in
 * NonCASRom/WinCE (a "Kernel Memory Usage" diagnostic).
 *
 * The original imports exactly three functions from COREDLL.DLL:
 *   NKDbgPrintfW (ordinal 545), SetKMode (630), SetProcPermissions (611)
 * and pokes kernel memory in kmode. Ours goes through the same imports
 * (resolved against our COREDLL.DLL export directory) and reports:
 *   - the SH7305 MMU/cache control state (direct privileged P4 reads,
 *     after asking for kmode via SetKMode/SetProcPermissions),
 *   - the system memory picture via GlobalMemoryStatus (which probes the
 *     NTOS heap for its largest free block),
 *   - an allocation soak test through LocalAlloc/LocalFree.
 */
#include "ntimport.h"

typedef unsigned int u32;

/* WinCE MEMORYSTATUS */
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

NT_IMPORT(void, NKDbgPrintfW, (const char *s))
NT_IMPORT(u32, SetKMode, (u32 enable))
NT_IMPORT(u32, SetProcPermissions, (u32 newperms))
NT_IMPORT(void, GlobalMemoryStatus, (MEMSTATUS *ms))
NT_IMPORT(void *, LocalAlloc, (u32 flags, u32 bytes))
NT_IMPORT(u32, LocalFree, (void *p))

NT_DEFINE(NKDbgPrintfW)
NT_DEFINE(SetKMode)
NT_DEFINE(SetProcPermissions)
NT_DEFINE(GlobalMemoryStatus)
NT_DEFINE(LocalAlloc)
NT_DEFINE(LocalFree)

/* privileged SH7305 control registers (P4 area) */
#define REG_MMUCR 0xFF000010u
#define REG_PTEH  0xFF000000u
#define REG_PTEL  0xFF000004u
#define REG_CCR   0xFF00001Cu

static u32 rd32(u32 addr)
{
    return *(volatile u32 *)addr;
}

static void puthex(const char *label, u32 v)
{
    char buf[40];
    const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = 0; label[i] != '\0' && i < (int)sizeof(buf) - 11; i++)
        buf[i] = label[i];
    buf[i++] = '0';
    buf[i++] = 'x';
    for (int k = 7; k >= 0; k--)
        buf[i++] = hex[(v >> (k * 4)) & 0xF];
    buf[i] = '\0';
    NT_CALL(NKDbgPrintfW, (buf));
}

/* Safe diagnostic path: no P4 control-register reads and no large heap
 * allocation search while NTOS is running with its UTLB mappings enabled.
 * Those operations can reset the calculator before any status is printed.
 * Restore them individually only after the basic import/return path works. */
int main(void)
{
    u32 prev_kmode, prev_perms;
    void *p;

    NT_CALL(NKDbgPrintfW, ("MEMTOOL: start"));

    prev_kmode = NT_CALL(SetKMode, (1));
    puthex("KModePrev=", prev_kmode);
    prev_perms = NT_CALL(SetProcPermissions, (0xFFFFFFFFu));
    puthex("ProcPermsPrev=", prev_perms);

    /* Small, bounded allocator smoke test rather than the old 12 MiB
     * GlobalMemoryStatus binary search and 1 MiB soak. */
    NT_CALL(NKDbgPrintfW, ("Heap: testing 4 KiB"));
    p = NT_CALL(LocalAlloc, (0, 4096u));
    if (p != 0) {
        NT_CALL(NKDbgPrintfW, ("Heap allocation OK"));
        NT_CALL(LocalFree, (p));
        NT_CALL(NKDbgPrintfW, ("Heap free OK"));
    } else {
        NT_CALL(NKDbgPrintfW, ("Heap allocation failed"));
    }

    NT_CALL(NKDbgPrintfW, ("MEMTOOL: done"));
    return 0;
}
