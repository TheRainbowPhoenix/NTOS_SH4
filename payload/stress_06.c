/* Stress DLL 06: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
unsigned stress_d06(unsigned value) {
    return value + 6u;
}
int main(void) { return 1; }
