/* Stress DLL 12: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
unsigned stress_d12(unsigned value)
{
    return value + 12u;
}
int main(void)
{
    return 1;
}
