/* Actual COREDLL WinCE GUI test: modal top ownership, clickable HWND,
 * two dialogs, draggable title, theme switching, TouchKB WM_CHAR, real
 * MessageBoxW IDOK/IDCANCEL. Uses the shipped code with fake screen/input.
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
static unsigned pixels,rects,app_events;
static unsigned fake_script;
static void pixel(u32 x,u32 y,u32 color) {
    assert(x<320u && y<528u && color<=65535u);++pixels;
}
static void rect(u32 x,u32 y,u32 r,u32 b,u32 c) {
    assert(x<=r && r<=320u && y<=b && b<=528u && c<=65535u);++rects;
}
static u32 input(u32 *x,u32 *y,u32 *d) {
    switch(fake_script++) {
    case 0: *x=210u;*y=282u;break; /* MB_OKCANCEL -> Cancel */
    case 1: *x=115u;*y=282u;break; /* MB_OK -> OK */
    default:assert(0 && "unexpected blocked modal input");break;
    }
    *d=1u;return 1u;
}
static int proc(void *h,u32 msg,u32 wp,u32 lp) {
    (void)h;(void)wp;(void)lp;
    if(msg==0x0201u || msg==0x0102u || msg==0x0100u)
        ++app_events;
    return 1;
}
static NT_MSG mouse(u32 type,int x,int y) {
    NT_MSG msg={0};msg.message=type;
    msg.pt.x=x;msg.pt.y=y;
    msg.lParam=((u32)y<<16)|(u32)x;
    return msg;
}
int main(void) {
    ntos_host_table[7]=(const void *)pixel;
    ntos_host_table[9]=(const void *)rect;
    ntos_host_table[15]=(const void *)input;
    NT_WNDCLASS cls={0};
    const u16 name[]={'D','l','g',0},title[]={'N','T','O','S',0};
    cls.class_name=name;cls.wndproc=proc;
    assert(RegisterClassW(&cls));
    void *parent=CreateWindowExW(0,name,title,0,0,0,320,528,0,0,0,0);
    assert(parent);
    assert(ShowWindow(parent,1)==0);
    assert(!NtosDialogOpen(0));
    assert(!NtosThemeSet(4u));
    NT_DLG_CONFIG cfg={sizeof(cfg),0u,55u,NT_DLG_ALIGN_CENTER,
                       0xFFFFFFFFu,0u,-1,-1,274,title};
    for(u32 theme=0u;theme<4u;++theme) {
        assert(NtosThemeSet(theme));
        u32 dlg=NtosDialogOpen(&cfg);
        assert(dlg);
        NT_DIALOG *d=nt_dlg_find(&g_nt_dialogs,dlg);
        assert(d && d->theme==theme && NtosThemeGet()==theme);
        NT_DLG_STYLE colors=nt_dlg_theme(theme);
        assert(colors.title && colors.border && colors.body);
        assert(NtosDialogEnd(dlg,NT_DIALOG_CANCEL));
        assert(NtosDialogResult(dlg)==NT_DIALOG_CANCEL);
        assert(NtosDialogRelease(dlg));
    }
    assert(NtosThemeSet(NT_DLG_THEME_WHITE));
    u32 first=NtosDialogOpen(&cfg);
    assert(first && NtosDialogTop()==first);
    NT_RECT fr={0};assert(NtosDialogGetRect(first,&fr));
    NT_DLG_ITEM label={sizeof(label),NT_DLG_LABEL,11u,
                       {12,12,220,33},{'L','a','b','e','l',0}};
    NT_DLG_ITEM edit={sizeof(edit),NT_DLG_EDIT,12u,
                      {12,40,220,70},{0}};
    NT_DLG_ITEM ok={sizeof(ok),NT_DLG_BUTTON,NT_DIALOG_OK,
                    {12,80,106,112},{'O','K',0}};
    NT_DLG_ITEM cancel={sizeof(cancel),NT_DLG_BUTTON,NT_DIALOG_CANCEL,
                        {116,80,220,112},{'C','a','n','c','e','l',0}};
    assert(NtosDialogAdd(first,&label));
    assert(NtosDialogAdd(first,&edit));
    assert(NtosDialogAdd(first,&ok));
    assert(NtosDialogAdd(first,&cancel));
    assert(NtosDialogSetText(first,11u,title));
    assert(!NtosDialogSetText(first,999u,title));
    u32 second=NtosDialogOpen(&cfg);
    assert(second && second!=first && NtosDialogTop()==second);
    /* Outside modal, click is swallowed; app receives nothing. */
    NT_MSG msg=mouse(0x0201u,0,0);
    assert(DispatchMessageW(&msg));
    assert(app_events==0u && NtosDialogResult(second)==0u);
    NT_RECT sr;assert(NtosDialogGetRect(second,&sr));
    /* Modal title supports dragging, with viewport bounds clamped. */
    msg=mouse(0x0201u,sr.left+12,sr.top+8);
    assert(DispatchMessageW(&msg));
    msg=mouse(0x0200u,308,501);
    assert(DispatchMessageW(&msg));
    assert(NtosDialogGetRect(second,&sr));
    assert(sr.right<=320 && sr.bottom<=528);
    msg=mouse(0x0202u,308,501);assert(DispatchMessageW(&msg));
    msg=mouse(0x0201u,sr.right-9,sr.top+8);
    assert(DispatchMessageW(&msg));
    assert(NtosDialogResult(second)==NT_DIALOG_CANCEL);
    assert(NtosDialogTop()==first);
    assert(NtosDialogRelease(second));
    /* Click a real bounded Edit control through the dialog overlay. */
    assert(NtosDialogGetRect(first,&fr));
    msg=mouse(0x0201u,fr.left+20,fr.top+24+51);
    assert(DispatchMessageW(&msg));
    assert(SipShowIM(1u));
    msg=(NT_MSG){0};msg.message=0x0102u;msg.wParam='H';
    assert(DispatchMessageW(&msg));
    msg.wParam='i';assert(DispatchMessageW(&msg));
    msg.wParam=8u;assert(DispatchMessageW(&msg));
    u16 out[16]={0};
    assert(NtosDialogGetText(first,out,16)==1 && out[0]=='H');
    assert(app_events==0u); /* normal HWND is input-blocked */
    /* Escape cancels topmost and restores prior keyboard state. */
    msg.message=0x0100u;msg.wParam=0x1Bu;
    assert(DispatchMessageW(&msg));
    assert(NtosDialogResult(first)==NT_DIALOG_CANCEL);
    assert((nt_sip_flags&1u)==0u && NtosDialogRelease(first));
    assert(NtosDialogTop()==0u);
    assert(pixels>10u && rects>10u);
    assert(DestroyWindow(parent));
    /* MessageBoxW is a synchronous WinCE caller, not a no-op stub. */
    const u16 body[]={'H','e','l','l','o',0};
    assert(MessageBoxW(0,body,title,1u)==2); /* IDCANCEL */
    assert(MessageBoxW(0,body,title,0u)==1); /* IDOK */
    assert(fake_script==2u);
    assert(MessageBoxW(0,body,title,4u)==0); /* MB_YESNO not faked */
    puts("PASS COREDLL modal: 4 themes, nested dialog top z-order, draggable title, modal blocked input, Edit+SIP WM_CHAR, MessageBoxW IDOK/IDCANCEL");
}
