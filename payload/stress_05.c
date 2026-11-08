/* Stress DLL 05: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
NT_IMPORT(unsigned,stress_d12,(unsigned));
NT_DEFINE(stress_d12)
NT_IMPORT(void,coredll_puts,(const char *));
NT_DEFINE(coredll_puts)
unsigned stress_d05(unsigned value) {
    NT_CALL(coredll_puts,("ST05 -> COREDLL dependency"));
    value=NT_CALL(stress_d12,(value));
    return value + 5u;
}
int main(void) { return 1; }
