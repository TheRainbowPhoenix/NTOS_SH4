/* Native SH4BE EXE: cause >55 UTLB misses across a DLL's data
 * segment; return a deterministic checksum to the NTOS picker. */
#include "ntimport.h"
NT_IMPORT(unsigned, pager_touch, (unsigned));
NT_DEFINE(pager_touch)
int main(void)
{
    return (int)NT_CALL(pager_touch, (0xB0000000u));
}
