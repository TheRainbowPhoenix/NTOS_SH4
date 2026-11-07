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

int main(void)
{
    MEMSTATUS ms;
    u32 prev_kmode, prev_perms;
    void *chunks[32];
    int nchunks;

    NT_CALL(NKDbgPrintfW, ("=== MEMTOOL: kernel memory usage ==="));

    /* ask for kernel mode + full permissions, like the original */
    prev_kmode = NT_CALL(SetKMode, (1));
    prev_perms = NT_CALL(SetProcPermissions, (0xFFFFFFFFu));
    puthex("KModePrev=", prev_kmode);
    puthex("ProcPermsPrev=", prev_perms);

    /* privileged register peek: MMU + cache control */
    puthex("MMUCR=", rd32(REG_MMUCR));
    puthex("PTEH=", rd32(REG_PTEH));
    puthex("PTEL=", rd32(REG_PTEL));
    puthex("CCR=", rd32(REG_CCR));

    /* system memory picture (GlobalMemoryStatus probes the heap) */
    ms.dwLength = sizeof(ms);
    NT_CALL(GlobalMemoryStatus, (&ms));
    puthex("TotalPhys=", ms.dwTotalPhys);
    puthex("AvailPhys=", ms.dwAvailPhys);
    puthex("MemLoad=", ms.dwMemLoad);

    /* allocation soak: 32 KiB chunks through LocalAlloc/LocalFree */
    nchunks = 0;
    while (nchunks < 32) {
        void *p = NT_CALL(LocalAlloc, (0, 32u * 1024u));
        if (p == 0)
            break;
        chunks[nchunks++] = p;
    }
    puthex("Alloc32K=", (u32)nchunks);
    while (nchunks > 0)
        NT_CALL(LocalFree, (chunks[--nchunks]));
    NT_CALL(NKDbgPrintfW, ("soak: all blocks freed"));

    NT_CALL(NKDbgPrintfW, ("=== MEMTOOL done ==="));
    return 0;
}
