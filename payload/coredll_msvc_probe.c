/* Marker present only in the isolated COREDLL MSVC ABI pilot.
 * The 5th argument must cross incoming MSVC S+16 correctly.
 */
#include <stdint.h>
uint32_t nt_native_coredll_abi5(uint32_t a,uint32_t b,uint32_t c,
                                uint32_t d,uint32_t e)
{
    return 0xC0DE0000u+(a+3u*b+5u*c+7u*d+11u*e);
}
