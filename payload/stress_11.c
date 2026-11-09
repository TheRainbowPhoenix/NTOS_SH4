/* Stress DLL 11: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
unsigned stress_d11(unsigned value)
{
    return value + 11u;
}
int main(void)
{
    return 1;
}
