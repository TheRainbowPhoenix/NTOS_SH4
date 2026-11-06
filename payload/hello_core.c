/* hello_core.c -- import-snapping demo payload for the NTOS UTLB loader.
 *
 * A real WinCE-style EXE: it imports its services from COREDLL (mapped and
 * snapped by NTOS), linked at its preferred base 0x00400000. NTOS maps the
 * image via the UTLB at 0x00400000, resolves coredll_puts/coredll_getkey
 * from COREDLL.NTX's export directory and fills the .idata slots before
 * the entry point runs.
 *
 * Entry contract (unchanged): r4 = &NtosApi, r5 = argv, r6 = envp. The
 * imports below work regardless because they go through coredll, which in
 * turn uses the 0x08100000 API window.
 */
#include "ntimport.h"

typedef unsigned int u32;

NT_IMPORT(void, coredll_puts, (const char *s));
NT_IMPORT(u32, coredll_getkey, (void));

NT_DEFINE(coredll_puts)
NT_DEFINE(coredll_getkey)

int main(u32 api, char **argv, char **envp)
{
    (void)api;
    (void)argv;
    (void)envp;

    NT_CALL(coredll_puts, ("Hello from a UTLB-mapped EXE"));
    NT_CALL(coredll_puts, ("imports snapped against COREDLL"));

    return 0x0F03BEE0;
}
