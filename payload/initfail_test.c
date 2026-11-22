/* INITFAIL.EXE: main must NEVER be called. Installer must
 * provide matching INITFAIL.DLL and INITGOOD.DLL; clean failure is a
 * launcher error 'DLL_PROCESS_ATTACH returned FALSE', NOT a checksum. */
#include <stdint.h>
#include "ntimport.h"
NT_IMPORT(uint32_t,initfail_probe,(void));
NT_DEFINE(initfail_probe)
int main(void) {
    (void)NT_CALL(initfail_probe,());
    return (int)0xBAD91003u;
}
