/* Stress DLL 03: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
NT_IMPORT(unsigned, stress_d08, (unsigned));
NT_DEFINE(stress_d08)
NT_IMPORT(unsigned, stress_d09, (unsigned));
NT_DEFINE(stress_d09)
unsigned stress_d03(unsigned value)
{
    value = NT_CALL(stress_d08, (value));
    value = NT_CALL(stress_d09, (value));
    return value + 3u;
}
int main(void)
{
    return 1;
}
