/* Shared system chrome dispatch for application windows. */
#ifndef HOSTED_HAS_PROCESS_IMPORT
NT_IMPORT(U32, CreateProcessW, (const W16 *, const W16 *, void *, void *, int, U32, void *, const W16 *, void *, void *))
NT_DEFINE(CreateProcessW)
#endif

static int hosted_shell_input(void *hwnd, U32 msg, U32 wp, U32 lp)
{
    static const W16 home[] = {'\\','f','l','s','0','\\','S','T','A','R','T','H','O','S','T','.','E','X','E',0};
    static const W16 search[] = {'\\','f','l','s','0','\\','A','p','p','s','\\','c','o','r','t','a','n','a','\\','c','o','r','t','a','n','a','.','e','x','e',0};
    static const W16 settings[] = {'\\','f','l','s','0','\\','A','p','p','s','\\','s','e','t','t','i','n','g','s','\\','s','e','t','t','i','n','g','s','.','e','x','e',0};
    int result;
    if (!g_chrome || (msg != WM_DOWN_ && msg != WM_MOVE_ && msg != WM_UP_ &&
                      !(msg == WM_KEY_ && g_chrome->expanded())))
        return 0;
    result = g_chrome->input(msg, wp, lp);
    if (!result)
        return 0;
    if (result == 2 || result == 3 || result == 5 || result == 6) {
        const W16 *path = result == 2 ? settings : result == 6 ? search : home;
        if (NT_CALL(CreateProcessW, (path, 0, 0, 0, 0, 0, 0, 0, 0, 0)))
            NT_CALL(PostQuitMessage, (0));
    } else if (result == 4) {
        nav_bar_action(result);
    }
    NT_CALL(InvalidateRect, (hwnd, 0, 0));
    NT_CALL(UpdateWindow, (hwnd));
    return 1;
}
