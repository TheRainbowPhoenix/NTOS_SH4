/* A genuine third-party WinCE-style DLL; the EXE imports CHAIN.DLL,
 * and CHAIN.DLL itself imports COREDLL.DLL. Exercises recursive discovery. */
#include "ntimport.h"
NT_IMPORT(void, coredll_puts, (const char *));
NT_DEFINE(coredll_puts)
unsigned chain_echo(unsigned x)
{
    NT_CALL(coredll_puts, ("CHAIN.DLL -> COREDLL.DLL: dependency linked"));
    return x ^ 0x13572468u;
}
/* Deliberate five-word ABI crossing: the fifth argument is on the stack.
 * The declaration carries the optional Renesas calling convention,
 * while chain_echo and COREDLL remain ordinary GCC-native code.
 * A real Microsoft SH4 reference must still verify stack/home layout. */
NT_CE_CANDIDATE_EXPORT(unsigned, chain_abi5,
                       (unsigned a, unsigned b, unsigned c, unsigned d, unsigned e));
unsigned chain_abi5(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e)
{
    const unsigned native_result = chain_echo(0x13572468u);
    return a ^ (b << 1) ^ (c << 2) ^ (d << 3) ^ (e << 4) ^ native_result;
}
int main(void)
{
    return 1;
}
