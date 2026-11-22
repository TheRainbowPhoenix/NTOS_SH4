/* Compile the actual Exword COREDLL native event branch on the host.
 * Each physical printable key must yield exactly one WM_CHAR; there must
 * never be a second queued character from TranslateMessage. HH3 tests
 * exercise their own original 320x528 input path separately.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define GetLastError nt_exword_test_GetLastError
#define SetLastError nt_exword_test_SetLastError
#define CloseHandle nt_exword_test_CloseHandle
#define LocalFree nt_exword_test_LocalFree
#define NTOS_CORE_HOST_TEST 1
#define NTOS_EXWORD 1
#define main nt_exword_test_core_entry
#include "../payload/coredll.c"
#undef main
const void *ntos_host_table[28];
static unsigned reads;
static u32 input(u32 *x,u32 *y,u32 *dir)
{
    static const u32 keys[]={'t',8u,'T',0x90u,'7',0x94u};
    assert(reads<sizeof(keys)/sizeof(keys[0]));
    *x=keys[reads++];*y=0u;*dir=0u;
    return 3u;
}
static int proc(void *hwnd,u32 message,u32 wparam,u32 lparam)
{
    (void)hwnd;(void)message;(void)wparam;(void)lparam;
    return 1;
}
int main(void)
{
    ntos_host_table[15]=(const void *)input;
    NT_WNDCLASS cls={0};
    static const u16 name[]={'T','e','s','t',0};
    static const u16 edit_name[]={'E','d','i','t',0};
    cls.wndproc=proc;cls.class_name=name;
    assert(RegisterClassW(&cls));
    void *win=CreateWindowExW(0,name,name,0,0,0,320,528,0,0,0,0);
    assert(win);
    NT_RECT bounds={0};
    assert(GetClientRect(win,&bounds));
    assert(bounds.right==528 && bounds.bottom==320);
    assert(GetSystemMetrics(0)==528 && GetSystemMetrics(1)==320);
    void *edit=CreateWindowExW(0,edit_name,0,0,10,32,240,28,win,0,0,0);
    assert(edit);
    ShowWindow(win,1);
    ShowWindow(edit,1);
    g_edit.focused=1;
    g_window.invalidated=0;
    const u32 expected_messages[]={0x102u,0x102u,0x102u,
                                   0x100u,0x102u,0x100u};
    const u32 expected_chars[]={'t',8u,'T',0x26u,'7',0x0du};
    for(unsigned n=0u;n<6u;++n) {
        NT_MSG msg={0};
        assert(GetMessageW(&msg,win,0u,0u)==1);
        assert(msg.message==expected_messages[n]);
        assert(msg.wParam==expected_chars[n]);
        (void)TranslateMessage(&msg);
        assert(g_qhead==g_qtail && "no extra WM_CHAR may be queued");
        DispatchMessageW(&msg);
        u16 text[12]={0};
        int len=GetWindowTextW(edit,text,12);
        if(n==0u)assert(len==1 && text[0]=='t');
        if(n==1u)assert(len==0);
        if(n==2u)assert(len==1 && text[0]=='T');
        if(n==3u)assert(len==1 && text[0]=='T');
        if(n==4u)assert(len==2 && text[0]=='T' && text[1]=='7');
        if(n==5u)assert(len==2 && text[1]=='7');
    }
    assert(reads==6u);
    assert(DestroyWindow(edit));
    assert(DestroyWindow(win));
    puts("PASS Exword WinCE physical QWERTY: one WM_CHAR per text/Del, 528x320, separate navigation");
}
