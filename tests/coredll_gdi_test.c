/* Tests the ACTUAL COREDLL memory GDI/BitBlt implementation, rather
 * than a duplicate model. x86 host stubs stand in for allocated P1
 * storage and the validated SH7305 RGB565 pixel service. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define GetLastError nt_test_GetLastError
#define SetLastError nt_test_SetLastError
#define CloseHandle nt_test_CloseHandle
#define LocalFree nt_test_LocalFree
#define NTOS_CORE_HOST_TEST
#define main coredll_dll_entry
#include "../payload/coredll.c"
#undef main
const void *ntos_host_table[28];
static unsigned allocated=0u,freed=0u,sent=0u;
static u32 last_x=0u,last_y=0u,last_rgb=0u;
static void *test_alloc(u32 n) {
    assert(n && n<=320u*528u*2u);
    ++allocated;return malloc(n);
}
static void test_free(void *p) {
    assert(p);++freed;free(p);
}
static void fake_pixel(u32 x,u32 y,u32 rgb) {
    assert(x<320u && y<528u && rgb<=0xFFFFu);
    ++sent;last_x=x;last_y=y;last_rgb=rgb;
}
static void reset_mock(void) {
    ntos_host_table[3]=(const void *)test_alloc;
    ntos_host_table[4]=(const void *)test_free;
    ntos_host_table[7]=(const void *)fake_pixel;
}
int main(void) {
    reset_mock();
    assert(!CreateCompatibleDC((void *)(uintptr_t)0xAABBCCDDu));
    assert(!CreateCompatibleBitmap((void *)(uintptr_t)0x03000001u,4,4));
    assert(!CreateCompatibleBitmap(GDI_SCREEN_DC,0,1));
    assert(!CreateCompatibleBitmap(GDI_SCREEN_DC,321,1));
    assert(!CreateCompatibleBitmap(GDI_SCREEN_DC,1,529));
    void *dc=CreateCompatibleDC(GDI_SCREEN_DC);
    void *other=CreateCompatibleDC(0);
    assert(dc && other && dc!=other);
    void *bmp=CreateCompatibleBitmap(dc,4,4);
    void *copy=CreateCompatibleBitmap(other,4,4);
    assert(bmp && copy && bmp!=copy && allocated==2u);
    assert(SelectObject(dc,bmp)==GDI_STOCK_BITMAP);
    assert(!SelectObject(other,bmp)); /* cannot have two DC owners */
    assert(!DeleteObject(bmp)); /* selected objects must survive */
    assert(SetPixel(dc,0,0,0x000000FFu)==0x000000FFu);
    assert(SetPixel(dc,1,0,0x0000FF00u)==0x0000FF00u);
    assert(SetPixel(dc,2,0,0x00FF0000u)==0x00FF0000u);
    assert(GetPixel(dc,0,0)==0x000000FFu);
    assert(GetPixel(dc,1,0)==0x0000FF00u);
    assert(GetPixel(dc,2,0)==0x00FF0000u);
    assert(GetPixel(dc,4,0)==GDI_INVALID_COLOR);
    assert(SetPixel(dc,-1,0,0)==GDI_INVALID_COLOR);
    assert(SetPixel((void *)(uintptr_t)0x03001234u,0,0,0)==GDI_INVALID_COLOR);
    void *brush=CreateSolidBrush(0x00FF0000u);
    NT_RECT rect={-2,1,2,3};
    assert(FillRect(dc,&rect,brush));
    assert(GetPixel(dc,0,1)==0x00FF0000u);
    assert(GetPixel(dc,1,2)==0x00FF0000u);
    assert(!FillRect((void *)(uintptr_t)0xDEADBEEFu,&rect,brush));
    assert(SelectObject(other,copy)==GDI_STOCK_BITMAP);
    assert(!BitBlt(other,0,0,4,4,dc,0,0,0xFFFFFFFFu));
    assert(BitBlt(other,0,0,4,4,dc,0,0,GDI_SRCCOPY));
    assert(GetPixel(other,2,0)==0x00FF0000u);
    assert(GetPixel(other,0,1)==0x00FF0000u);
    /* Overlap copies must act like memmove, including right shifts. */
    assert(BitBlt(dc,1,0,3,1,dc,0,0,GDI_SRCCOPY));
    assert(GetPixel(dc,0,0)==0x000000FFu);
    assert(GetPixel(dc,1,0)==0x000000FFu);
    assert(GetPixel(dc,2,0)==0x0000FF00u);
    assert(GetPixel(dc,3,0)==0x00FF0000u);
    /* Opposite direction copies and vertical overlapping copies. */
    assert(BitBlt(dc,0,0,3,1,dc,1,0,GDI_SRCCOPY));
    assert(GetPixel(dc,0,0)==0x000000FFu &&
           GetPixel(dc,1,0)==0x0000FF00u &&
           GetPixel(dc,2,0)==0x00FF0000u);
    assert(BitBlt(dc,0,1,4,3,dc,0,0,GDI_SRCCOPY));
    assert(GetPixel(dc,0,1)==0x000000FFu);
    assert(GetPixel(dc,0,2)==0x00FF0000u);
    /* Screen has no readback: only memory-source SRCCOPY is supported. */
    assert(!BitBlt(dc,0,0,4,4,GDI_SCREEN_DC,0,0,GDI_SRCCOPY));
    assert(!BitBlt(GDI_SCREEN_DC,0,0,4,4,dc,0,0,0x00F00021u));
    assert(BitBlt(GDI_SCREEN_DC,315,524,4,4,dc,0,0,GDI_SRCCOPY));
    assert(sent==16u && last_x==318u && last_y==527u);
    assert(last_rgb<=0xFFFFu);
    assert(BitBlt(GDI_SCREEN_DC,319,527,4,4,dc,0,0,GDI_SRCCOPY));
    assert(sent==17u && last_x==319u && last_y==527u);
    assert(!DeleteObject(copy));
    assert(DeleteDC(other));
    assert(DeleteObject(copy) && !DeleteObject(copy));
    assert(GetPixel(other,0,0)==GDI_INVALID_COLOR);
    assert(!DeleteDC(other));
    assert(SelectObject(dc,GDI_STOCK_BITMAP)==bmp);
    assert(DeleteObject(bmp));
    assert(!DeleteObject(bmp));
    /* WinCE nearest-neighbour no-AA scaling, 2x2 -> 6x6. */
    void *scale_dc=CreateCompatibleDC(GDI_SCREEN_DC);
    void *scale_bmp=CreateCompatibleBitmap(GDI_SCREEN_DC,6,6);
    assert(scale_dc && scale_bmp && SelectObject(scale_dc,scale_bmp)==GDI_STOCK_BITMAP);
    void *source_dc=CreateCompatibleDC(GDI_SCREEN_DC);
    void *source_bmp=CreateCompatibleBitmap(GDI_SCREEN_DC,2,2);
    assert(source_dc && source_bmp && SelectObject(source_dc,source_bmp)==GDI_STOCK_BITMAP);
    assert(SetPixel(source_dc,0,0,0x000000FFu)==0x000000FFu);
    assert(SetPixel(source_dc,1,0,0x0000FF00u)==0x0000FF00u);
    assert(SetPixel(source_dc,0,1,0x00FF0000u)==0x00FF0000u);
    assert(SetPixel(source_dc,1,1,0x00FFFFFFu)==0x00FFFFFFu);
    assert(StretchBlt(scale_dc,0,0,6,6,source_dc,0,0,2,2,GDI_SRCCOPY));
    assert(GetPixel(scale_dc,0,0)==0x000000FFu);
    assert(GetPixel(scale_dc,2,2)==0x000000FFu);
    assert(GetPixel(scale_dc,3,0)==0x0000FF00u);
    assert(GetPixel(scale_dc,5,2)==0x0000FF00u);
    assert(GetPixel(scale_dc,0,3)==0x00FF0000u);
    assert(GetPixel(scale_dc,3,3)==0x00FFFFFFu);
    assert(!StretchBlt(source_dc,0,0,2,2,source_dc,0,0,1,1,GDI_SRCCOPY));
    assert(!StretchBlt(scale_dc,0,0,6,6,GDI_SCREEN_DC,0,0,2,2,GDI_SRCCOPY));
    assert(!StretchBlt(scale_dc,0,0,6,6,source_dc,0,0,2,2,0xFFFFFFFFu));
    assert(StretchBlt(GDI_SCREEN_DC,318,526,6,6,source_dc,0,0,2,2,GDI_SRCCOPY));
    assert(SelectObject(source_dc,GDI_STOCK_BITMAP)==source_bmp);
    assert(SelectObject(scale_dc,GDI_STOCK_BITMAP)==scale_bmp);
    assert(DeleteObject(source_bmp) && DeleteObject(scale_bmp));
    assert(DeleteDC(source_dc) && DeleteDC(scale_dc));
    assert(DeleteDC(dc));
    assert(!DeleteDC(dc));
    assert(allocated==freed && allocated==4u); /* two GDI and two scale bitmaps */
    /* Slot reuse cannot resurrect an earlier opaque handle. */
    void *again=CreateCompatibleBitmap(GDI_SCREEN_DC,4,4);
    assert(again && again!=bmp);
    assert(!DeleteObject(bmp));
    assert(DeleteObject(again));
    assert(allocated==freed);
    /* Intentionally leave a selected bitmap (plus an unselected one)
       allocated. The real exported DLL entrypoint MUST release both
       even if the application never issued DeleteDC/DeleteObject. */
    void *leaked_dc=CreateCompatibleDC(GDI_SCREEN_DC);
    void *leaked_bmp=CreateCompatibleBitmap(GDI_SCREEN_DC,16,16);
    void *leaked_extra=CreateCompatibleBitmap(GDI_SCREEN_DC,5,6);
    assert(leaked_dc && leaked_bmp && leaked_extra);
    assert(SelectObject(leaked_dc,leaked_bmp)==GDI_STOCK_BITMAP);
    assert(!DeleteObject(leaked_bmp));
    assert(coredll_dll_entry((void *)(uintptr_t)0x10000000u,0u,0)==1);
    assert(allocated==freed);
    assert(!DeleteDC(leaked_dc));
    assert(!DeleteObject(leaked_bmp) && !DeleteObject(leaked_extra));
    /* A new process may reuse table slots but never a stale token. */
    void *fresh=CreateCompatibleBitmap(GDI_SCREEN_DC,8,8);
    assert(fresh && fresh!=leaked_bmp && fresh!=leaked_extra);
    assert(DeleteObject(fresh) && allocated==freed);
    puts("PASS COREDLL GDI: WinCE memory DC RGB565, SRCCOPY clipping/overlap, monotonic typed handles and forced DllMain detach cleanup");
    return 0;
}
