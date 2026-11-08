/* Stress DLL 09: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
unsigned stress_d09(unsigned value) {
    return value + 9u;
}
int main(void) { return 1; }
