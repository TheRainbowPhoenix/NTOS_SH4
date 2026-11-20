/* INITFAIL.DLL imports INITGOOD.DLL so GOOD must initialize first.
 * Returning FALSE from PROCESS_ATTACH must suppress EXE entry, and
 * roll back GOOD with DLL_PROCESS_DETACH exactly once. */
#include <stdint.h>
#include "ntimport.h"
NT_IMPORT(uint32_t,initgood_state,(void));
NT_DEFINE(initgood_state)
uint32_t initfail_probe(void) {return 0xBAD91002u;}
int main(uint32_t module,uint32_t reason,void *reserved) {
    (void)module;(void)reserved;
    if(reason==1u) {
        // Only a fully initialized dependency may reach here.
        return NT_CALL(initgood_state,())==1u ? 0 : 1;
    }
    return 1;
}
