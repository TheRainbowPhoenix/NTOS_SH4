/* Host test compiles the ACTUAL COREDLL implementation and drives
 * native WinCE CreateWindowExW(Edit)/SIP/WM_CHAR/NTOS GWES policy.
 * Strictly no mocked duplicate policy implementation.
 */
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
static unsigned pixels,rects,events,commands;
static void pixel(u32 x,u32 y,u32 color) {
    assert(x<320u && y<528u && color<=65535u);++pixels;
}
static void fill(u32 l,u32 t,u32 r,u32 b,u32 color) {
    assert(l<=r && r<=320u && t<=b && b<=528u && color<=65535u);++rects;
}
static u32 fake_touch(u32 *x,u32 *y,u32 *dir) {
    assert(events++<8u);
    *x=3u;*y=320u;*dir=1u;
    return 1u;
}
static int proc(void *h,u32 msg,u32 wp,u32 lp) {
    (void)h;(void)wp;(void)lp;
    if(msg==0x0111u)++commands;
    return 1;
}
int main(void) {
    ntos_host_table[7]=(const void *)pixel;
    ntos_host_table[9]=(const void *)fill;
    ntos_host_table[15]=(const void *)fake_touch;
    assert(NtosGwesGetMode()==NT_GWES_FULL);
    u32 first=NtosGwesCreate(),second=NtosGwesCreate(),action=0;
    assert(first && second && first!=second && !NtosGwesCreate());
    NT_RECT rect;
    assert(NtosGwesGetRect(first,&rect) && rect.right==320 && rect.bottom==528);
    assert(!NtosGwesGetRect(second,&rect));
    assert(NtosGwesSetMode(NT_GWES_STACK) && NtosGwesGetMode()==NT_GWES_STACK);
    assert(NtosGwesGetRect(first,&rect) && rect.bottom==264);
    assert(NtosGwesGetRect(second,&rect) && rect.top==264);
    assert(NtosGwesSetMode(NT_GWES_COLUMNS));
    assert(NtosGwesGetRect(second,&rect) && rect.left==160);
    assert(NtosGwesHit(310,2,&action)==second && action==NT_GWES_CLOSE_HIT);
    assert(NtosGwesSwap());
    assert(NtosGwesGetRect(second,&rect) && rect.left==0);
    assert(!NtosGwesFocus(0xdeadbeefu));
    assert(NtosGwesKeyboard(1u));
    assert(NtosGwesGetRect(second,&rect) && rect.bottom==271);
    NT_SIP_INFO sip={0};
    sip.size=sizeof(sip);
    assert(SipGetInfo(&sip) && (sip.flags&1u) && sip.desktop.bottom==271);
    assert(!NtosGwesHit(2,320,&action));
    assert(NtosGwesKeyboard(0u));
    assert(SipGetInfo(&sip) && !(sip.flags&1u) &&
           sip.desktop.bottom==528);
    assert(!NtosGwesKeyboard(3u));
    assert(NtosGwesClose(first) && !NtosGwesClose(first));
    assert(NtosGwesClose(second));
    assert(NtosGwesReset());
    u32 new_id=NtosGwesCreate();
    assert(new_id!=first && new_id!=second);
    NT_WNDCLASS cl={0};
    const u16 name[]={'W','i','n',0};
    const u16 title[]={'W','i','n','d','o','w',0};
    const u16 editname[]={'E','d','i','t',0};
    cl.class_name=name;cl.wndproc=proc;
    assert(RegisterClassW(&cl));
    void *win=CreateWindowExW(0,name,title,0,0,0,320,528,0,0,0,0);
    assert(win);
    void *edit=CreateWindowExW(0,editname,0,0,12,64,295,32,
                               win,0,0,0);
    assert(edit && edit!=win);
    assert(!CreateWindowExW(0,editname,0,0,12,64,100,32,win,0,0,0));
    assert(ShowWindow(edit,1)==0);
    assert(GetClientRect(edit,&rect) && rect.right==295 && rect.bottom==32);
    NT_MSG tap={0};
    tap.hwnd=win;tap.message=0x0201u;tap.pt.x=20;tap.pt.y=72;
    assert(DispatchMessageW(&tap));
    assert(SipGetInfo(&sip) && (sip.flags&1u));
    /* Ignore the WinCE WM_SETTINGCHANGE queued by SipShowIM: test the
       next real hardware-style touch -> COREDLL WM_CHAR conversion. */
    g_qhead=g_qtail;g_window.invalidated=0;
    NT_MSG msg={0};
    assert(GetMessageW(&msg,win,0,0)==1);
    assert(msg.message==0x0102u && msg.wParam=='q');
    assert(DispatchMessageW(&msg));
    u16 out[12]={0};
    assert(GetWindowTextW(edit,out,12)==1 && out[0]=='q' && out[1]==0);
    assert(commands==1);
    msg.message=0x0102u;msg.wParam='W';
    assert(DispatchMessageW(&msg));
    assert(GetWindowTextW(edit,out,12)==2 && out[1]=='W');
    msg.wParam=8u;assert(DispatchMessageW(&msg));
    assert(GetWindowTextW(edit,out,12)==1 && out[0]=='q');
    const u16 hello[]={'H','e','l','l','o',0};
    assert(SetWindowTextW(edit,hello));
    assert(GetWindowTextW(edit,out,12)==5 && out[0]=='H' && out[4]=='o');
    assert(UpdateWindow(edit));
    assert(rects>0u && pixels>0u);
    assert(DestroyWindow(edit));
    assert(!GetWindowTextW(edit,out,12));
    assert(DestroyWindow(win));
    assert(!DestroyWindow(win));
    puts("PASS actual COREDLL GWES/SIP: two logical handles, fullscreen/stack/columns, focus, docked TouchKB WM_CHAR, WinCE Edit control, stale handles");
}
