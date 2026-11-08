/* A genuine third-party WinCE-style DLL; the EXE imports CHAIN.DLL,
 * and CHAIN.DLL itself imports COREDLL.DLL. Exercises recursive discovery. */
#include "ntimport.h"
NT_IMPORT(void,coredll_puts,(const char *));
NT_DEFINE(coredll_puts)
unsigned chain_echo(unsigned x) {
    NT_CALL(coredll_puts,("CHAIN.DLL -> COREDLL.DLL: dependency linked"));
    return x ^ 0x13572468u;
}
int main(void) { return 1; }
