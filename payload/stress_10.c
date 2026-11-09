/* Stress DLL 10: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
unsigned stress_d10(unsigned value)
{
    return value + 10u;
}
int main(void)
{
    return 1;
}
