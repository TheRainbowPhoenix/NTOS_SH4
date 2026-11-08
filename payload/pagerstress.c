/* Hardware demand-paging test DLL: 80 distinct 4KiB BSS pages.
 * Gated fault build maps only hot pages, then exercises 80 faults and
 * verifies data survives UTLB eviction. No FPU, no CRT, no SDK in ISR. */
#include <stdint.h>
static volatile uint8_t pages[80u*4096u];
uint32_t pager_touch(uint32_t seed) {
    uint32_t sum=seed;
    for(uint32_t i=0;i<80u;++i) {
        pages[i*4096u]=(uint8_t)(i+1u);
        sum+=(uint32_t)pages[i*4096u];
    }
    for(uint32_t i=0;i<80u;++i)
        sum^=((uint32_t)pages[i*4096u])<<((i&3u)*8u);
    return sum;
}
int main(void) { return 1; }
