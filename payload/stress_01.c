/* Stress DLL 01: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
NT_IMPORT(unsigned, stress_d02, (unsigned));
NT_DEFINE(stress_d02)
NT_IMPORT(unsigned, stress_d03, (unsigned));
NT_DEFINE(stress_d03)
NT_IMPORT(unsigned, stress_d04, (unsigned));
NT_DEFINE(stress_d04)
NT_IMPORT(unsigned, stress_d05, (unsigned));
NT_DEFINE(stress_d05)
unsigned stress_d01(unsigned value)
{
    value = NT_CALL(stress_d02, (value));
    value = NT_CALL(stress_d03, (value));
    value = NT_CALL(stress_d04, (value));
    value = NT_CALL(stress_d05, (value));
    return value + 1u;
}
int main(void)
{
    return 1;
}
