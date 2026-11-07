/* MEMTOOL.EXE - WinCE COREDLL diagnostic, SH4 BE.
 * Stage markers distinguish the PE entry ABI, COREDLL string dispatch,
 * and memory allocation. No P4 reads or large heap probes in this build. */
#include "ntimport.h"
typedef unsigned int u32;
typedef unsigned short WCHAR16;
typedef struct {
    void (*puts)(const char *);
    void *reserved[10];
} NTOS_API;

NT_IMPORT(void, OutputDebugStringW, (const WCHAR16 *s))
NT_IMPORT(u32, SetKMode, (u32 enable))
NT_IMPORT(u32, SetProcPermissions, (u32 permissions))
NT_IMPORT(void *, LocalAlloc, (u32 flags, u32 bytes))
NT_IMPORT(u32, LocalFree, (void *ptr))

NT_DEFINE(OutputDebugStringW)
NT_DEFINE(SetKMode)
NT_DEFINE(SetProcPermissions)
NT_DEFINE(LocalAlloc)
NT_DEFINE(LocalFree)

static void out_w(const char *narrow)
{
    WCHAR16 w[64];
    unsigned n=0;
    while (n < 63 && narrow[n]) {
        w[n] = (unsigned char)narrow[n];
        ++n;
    }
    w[n]=0;
    NT_CALL(OutputDebugStringW, (w));
}

static void hex_w(const char *label, u32 value)
{
    char buf[40];
    static const char hexdigits[] = "0123456789ABCDEF";
    unsigned n=0;
    while (label[n] && n < sizeof(buf)-11) {
        buf[n]=label[n]; ++n;
    }
    buf[n++]='0'; buf[n++]='x';
    for (int shift=28; shift>=0; shift-=4)
        buf[n++]=hexdigits[(value >> shift) & 15u];
    buf[n]=0;
    out_w(buf);
}

/* The loader passes the NTOS API table in r4. Keep the NTOS output path
 * separate from imported COREDLL calls to expose import/ABI failures. */
int main(const NTOS_API *api, char **argv, char **envp)
{
    void *p;
    (void)argv; (void)envp;
    if (api && api->puts) api->puts("MEMTOOL: entry reached");
    out_w("MEMTOOL: COREDLL debug");
    hex_w("KModePrev=", NT_CALL(SetKMode, (1)));
    hex_w("ProcPermsPrev=", NT_CALL(SetProcPermissions, (0xFFFFFFFFu)));
    p = NT_CALL(LocalAlloc, (0, 4096u));
    if (p) {
        out_w("LocalAlloc 4K OK");
        NT_CALL(LocalFree, (p));
        out_w("LocalFree OK");
    } else {
        out_w("LocalAlloc failed");
    }
    if (api && api->puts) api->puts("MEMTOOL: returning zero");
    return 0;
}
