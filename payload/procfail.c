/* PROCFAIL.EXE: optional *missing dependency* negative loader test.
 * DO NOT install NOFILE.DLL. Loader must reject this before calling main.
 * A failed import is NOT the same as a failed DllMain initialization. */
#include "ntimport.h"
NT_IMPORT(unsigned, missing_probe_entry, (void));
NT_DEFINE(missing_probe_entry)
int main(void)
{
    // Must never be reached if NOFILE.DLL is genuinely absent.
    return (int)NT_CALL(missing_probe_entry, ());
}
