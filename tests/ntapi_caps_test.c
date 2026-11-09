#include <assert.h>
#include <stdio.h>
#include "ntapi_caps.h"
int main(void)
{
    uint32_t window[4]={0x8cff7a08u,0,0,0};
    /* Reproduces the old pager's zero metadata: the table ends at 23,
     * followed by SDK fixup data, including Battery_GetVoltage text. */
    assert(!ntos_api_has_slot(window,24));
    assert(!ntos_api_has_slot(window,25));
    window[1]=NTOS_API_MAGIC;window[2]=NTOS_API_VERSION;window[3]=24;
    assert(!ntos_api_has_slot(window,24));
    window[3]=25;
    assert(ntos_api_has_slot(window,24));
    assert(!ntos_api_has_slot(window,25));
    window[3]=27;
    assert(ntos_api_has_slot(window,24));
    assert(ntos_api_has_slot(window,25));
    assert(ntos_api_has_slot(window,26));
    assert(!ntos_api_has_slot(window,27));
    window[3]=0xffffffffu;
    assert(!ntos_api_has_slot(window,25));
    window[3]=27;window[2]=0;
    assert(!ntos_api_has_slot(window,25));
    puts("PASS legacy, truncated, current and corrupt NTOS API capabilities");
    return 0;
}
