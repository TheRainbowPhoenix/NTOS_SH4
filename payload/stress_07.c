/* Stress DLL 07: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
unsigned stress_d07(unsigned value) {
    return value + 7u;
}
int main(void) { return 1; }
