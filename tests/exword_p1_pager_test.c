/* Exword's PAGERTEST is an 80-page eager-P1 data integrity probe,
 * never a TLB-fault test. Exercise the ACTUAL DLL source with a supplied
 * host table (not the ClassPad P0 0x08100000 API window). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define NTOS_EXWORD_P1_VM 1
#include "../payload/pagerstress.c"
static unsigned touched;
static int page_hint(uint32_t addr)
{
    (void)addr;
    ++touched;
    return 0; /* Every page of this test DLL is already P1-backed. */
}
int main(void)
{
    const void *table[64]={0};
    table[23]=(const void *)page_hint;
    assert(pager_touch(0xB0000000u,0)==0xBAD10003u);
    assert(touched==0u);
    assert(pager_touch(0xB0000000u,table)==0xE0000CA8u);
    assert(touched==160u);
    assert(pager_touch(0xB0000000u,table)==0xE0000CA8u);
    assert(touched==320u);
    puts("PASS Exword P1 PAGERTEST checksum 0xE0000CA8, 80 pages, no P0 API/UTLB faults");
    return 0;
}
