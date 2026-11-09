/* Stress DLL 04: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
NT_IMPORT(unsigned, stress_d10, (unsigned));
NT_DEFINE(stress_d10)
NT_IMPORT(unsigned, stress_d11, (unsigned));
NT_DEFINE(stress_d11)
unsigned stress_d04(unsigned value)
{
    value = NT_CALL(stress_d10, (value));
    value = NT_CALL(stress_d11, (value));
    return value + 4u;
}
int main(void)
{
    return 1;
}
