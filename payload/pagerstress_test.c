/* Native SH4BE EXE: cause >55 UTLB misses across a DLL's data
 * segment; return a deterministic checksum to the NTOS picker. */
#include "ntimport.h"
#ifdef NTOS_EXWORD_P1_VM
NT_IMPORT(unsigned, pager_touch, (unsigned,const void *const *));
#else
NT_IMPORT(unsigned, pager_touch, (unsigned));
#endif
NT_DEFINE(pager_touch)
#ifdef NTOS_EXWORD_P1_VM
int main(const void *const *api, char **argv,char **envp)
{
    (void)argv;(void)envp;
    return (int)NT_CALL(pager_touch,(0xB0000000u,api));
}
#else
int main(void)
{
    return (int)NT_CALL(pager_touch, (0xB0000000u));
}
#endif
