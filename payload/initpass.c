/* INITPASS.EXE: imports a DLL whose PROCESS_ATTACH must run
 * before EXE entry, and returns a deterministic success checksum. */
#include <stdint.h>
#include "ntimport.h"
NT_IMPORT(uint32_t,initgood_state,(void));
NT_DEFINE(initgood_state)
int main(void) {
    return NT_CALL(initgood_state,())==1u ?
           (int)0xD11A0001u : (int)0xBAD91001u;
}
