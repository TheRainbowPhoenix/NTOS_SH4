#include <assert.h>
#define main nt_test_commctrl_dllmain
#include "../payload/commctrl_native.c"
#undef main

int main(void)
{
    INITCOMMONCONTROLSEX i = {sizeof(i), NT_ICC_STANDARD_CLASSES};
    InitCommonControls();
    assert(InitCommonControlsEx(&i));
    i.dwICC = NT_ICC_PROGRESS_CLASS;
    assert(InitCommonControlsEx(&i));
    i.dwICC = NT_ICC_SUPPORTED;
    assert(InitCommonControlsEx(&i));
    i.dwICC = 0x1u;
    assert(!InitCommonControlsEx(&i)); /* no LISTVIEW yet */
    i.dwICC = 0xffu;
    assert(!InitCommonControlsEx(&i)); /* no Win95-all claim */
    i.dwICC = 0u;
    assert(!InitCommonControlsEx(&i));
    i.dwICC = NT_ICC_PROGRESS_CLASS;
    i.dwSize = 4u;
    assert(!InitCommonControlsEx(&i));
    assert(!InitCommonControlsEx(0));
    assert(nt_test_commctrl_dllmain() == 1);
    return 0;
}
