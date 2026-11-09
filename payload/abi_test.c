/* Five-word import/export ABI probe. A=1 B=2 C=3 D=4 E=5.
 * Expected result: 1 ^ 4 ^ 12 ^ 32 ^ 80 = 0x79.
 * Run in emulator after building with make ce-abi-probe.
 * This tests the GCC Renesas ABI on both sides, NOT Microsoft ABI yet.
 */
#include "ntimport.h"

NT_IMPORT_CE_CANDIDATE(unsigned, chain_abi5, (unsigned, unsigned, unsigned, unsigned, unsigned));
NT_DEFINE(chain_abi5)

int main(void)
{
    const unsigned result = NT_CALL(chain_abi5, (1u, 2u, 3u, 4u, 5u));
    return result == 0x79u ? (int)0x0AB1C0DEu : 0;
}
