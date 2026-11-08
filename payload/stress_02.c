/* Stress DLL 02: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
NT_IMPORT(unsigned,stress_d06,(unsigned));
NT_DEFINE(stress_d06)
NT_IMPORT(unsigned,stress_d07,(unsigned));
NT_DEFINE(stress_d07)
unsigned stress_d02(unsigned value) {
    value=NT_CALL(stress_d06,(value));
    value=NT_CALL(stress_d07,(value));
    return value + 2u;
}
int main(void) { return 1; }
