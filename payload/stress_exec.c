/* 12-DLL dependency stress: ST01 requires 4 DLLs; transitive 12 nodes
 * with ST05 importing COREDLL. No FPU and no host C runtime. */
#include "ntimport.h"
NT_IMPORT(unsigned, stress_d01, (unsigned));
NT_DEFINE(stress_d01)
int main(void)
{
    return (int)NT_CALL(stress_d01, (0x12340000u));
}
