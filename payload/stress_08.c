/* Stress DLL 08: shared preferred ImageBase 0x10000000. */
#include "ntimport.h"
unsigned stress_d08(unsigned value) {
    return value + 8u;
}
int main(void) { return 1; }
