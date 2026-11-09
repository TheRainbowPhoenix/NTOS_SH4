/* Isolated CE profile: reuse NTOS COREDLL; add only bounded services for probes. */
#define main ntos_core_entry
#define CreateFileW ntos_CreateFileW
#define ReadFile ntos_ReadFile
#define WriteFile ntos_WriteFile
#define CloseHandle ntos_CloseHandle
#define LocalAlloc ntos_LocalAlloc
#define LocalFree ntos_LocalFree
#define CreateWindowExW ntos_CreateWindowExW
#define DestroyWindow ntos_DestroyWindow
#define ShowWindow ntos_ShowWindow
#define GetClientRect ntos_GetClientRect
#define UpdateWindow ntos_UpdateWindow
#define InvalidateRect ntos_InvalidateRect
#define PostMessageW ntos_PostMessageW
#define DispatchMessageW ntos_DispatchMessageW
#define GetSystemMetrics ntos_GetSystemMetrics
#define FindFirstFileW ntos_FindFirstFileW
#define RegisterClassW ntos_RegisterClassW
#include "../coredll.c"
#undef LocalAlloc
#undef LocalFree
#undef CreateWindowExW
#undef DestroyWindow
#undef ShowWindow
#undef GetClientRect
#undef UpdateWindow
#undef InvalidateRect
#undef PostMessageW
#undef DispatchMessageW
#undef GetSystemMetrics
#undef FindFirstFileW
#undef RegisterClassW
#undef main
#undef CreateFileW
#undef ReadFile
#undef WriteFile
#undef CloseHandle
static void *ce_app_window_proc;
static int ce_msvc_window_proc(void *, u32, u32, u32);
static u32 ce_failed, ce_cookie = 0x12345678;
static unsigned ce_input;
static const char input_script[] = "?\r\nexit\r\n";
static int wide_equal(const u16 *a, const char *b)
{
    while (*a && *b) {
        if (*a++ != (unsigned char)*b++)
            return 0;
    }
    return !*a && !*b;
}
u32 CeUnsupported(u32 ordinal)
{
    ce_failed = ordinal;
    g_last_error = 120;
    coredll_puts("CE: unsupported ordinal reached");
    return 0;
}
u32 CeFailure(void)
{
    return ce_failed;
}
u32 __security_gen_cookie2(void)
{
    return ce_cookie;
}
u32 __GetUserKData(u32 offset)
{
    static u32 tls[64];
    if (offset == 12)
        return 1;
    if (offset == 20)
        return (u32)tls;
    return 0;
}
u32 DisableThreadLibraryCalls(void *m)
{
    (void)m;
    return 1;
}
u32 DeleteCriticalSection(void *p)
{
    (void)p;
    return 0;
}
u32 InterlockedExchange(volatile u32 *p, u32 v)
{
    u32 old = *p;
    *p = v;
    return old;
}
u32 TerminateProcess(void *p, u32 result)
{
    (void)p;
    (void)result;
    return 1;
}
u32 OpenProcess(u32 access, u32 inherit, u32 pid)
{
    (void)access;
    (void)inherit;
    if (pid != 1) {
        g_last_error = 87;
        return 0;
    }
    return 1;
}
u32 GetCurrentProcessId(void)
{
    return 1;
}
u32 GetCurrentThreadId(void)
{
    return 1;
}
u32 GetHeapSnapshot(void *p)
{
    (void)p;
    return 0;
}
static int readable(u32 p, u32 n)
{
    if (!p || n > 0x10000u || p + n < p)
        return 0;
    const u32 bounds[][2] = {{0x10000, 0x40000},       {0x400000, 0x410000},
                             {0xc000000, 0xc400000},   {0x10000000, 0x10040000},
                             {0x400a0000, 0x400a7000}, {0x8c000000, 0x8d000000}};
    for (unsigned i = 0; i < sizeof(bounds) / sizeof(bounds[0]); ++i)
        if (p >= bounds[i][0] && p + n <= bounds[i][1])
            return 1;
    return 0;
}
u32 ReadProcessMemory(u32 process, const void *src, void *dst, u32 n, u32 *done)
{
    if (done)
        *done = 0;
    if (process != 1 || !dst || !readable((u32)src, n)) {
        g_last_error = process != 1 ? 87 : 998;
        return 0;
    }
    for (u32 i = 0; i < n; ++i)
        ((unsigned char *)dst)[i] = ((const unsigned char *)src)[i];
    if (done)
        *done = n;
    g_last_error = 0;
    return 1;
}
static void setwide(u16 *d, const char *s)
{
    while (*s)
        *d++ = (unsigned char)*s++;
    *d = 0;
}
void *THCreateSnapshot(u32 flags, u32 pid)
{
    if (pid && pid != 1) {
        g_last_error = 87;
        return 0;
    }
    unsigned pc = (flags & 2) ? 1 : 0, mc = (flags & 8) ? 1 : 0, tc = (flags & 4) ? 1 : 0;
    u32 size = 0x3c + pc * 0x234 + mc * 0x434 + tc * 0x24;
    unsigned char *b = coredll_malloc(size);
    if (!b)
        return 0;
    for (u32 i = 0; i < size; ++i)
        b[i] = 0;
    u32 *h = (u32 *)b;
    h[4] = pc;
    h[5] = mc;
    h[6] = tc;
    if (pc) {
        u32 *p = (u32 *)(b + 0x3c);
        p[0] = 0x234;
        p[1] = 1;
        p[2] = 1;
        p[5] = 1;
        p[7] = 128;
        setwide((u16 *)(p + 10), "CEPROBE.EXE");
    }
    if (mc) {
        u32 *p = (u32 *)(b + 0x3c + pc * 0x234);
        p[0] = 0x434;
        p[1] = 1;
        p[2] = 1;
        p[3] = p[4] = 1;
        p[5] = 0x400000;
        p[6] = 0x10000;
        p[7] = 0x400000;
        setwide((u16 *)(p + 8), "CEPROBE.EXE");
        setwide((u16 *)(p + 8) + 260, "\\fls0\\CEPROBE.EXE");
    }
    if (tc) {
        u32 *p = (u32 *)(b + 0x3c + pc * 0x234 + mc * 0x434);
        p[0] = 0x24;
        p[1] = 1;
        p[2] = 1;
        p[3] = 1;
        p[4] = 128;
    }
    return b;
}
void *CreateFileW(const u16 *p, u32 access, u32 share, void *security, u32 creation, u32 attrs,
                  void *templ)
{
    if (wide_equal(p, "\\Release\\con") || wide_equal(p, "CON:") || wide_equal(p, "con"))
        return (void *)0x70000001u;
    return ntos_CreateFileW(p, access, share, security, creation, attrs, templ);
}
int CloseHandle(void *h)
{
    if ((u32)h == 1u || (u32)h == 0x70000001u || (u32)h == 0x70000002u)
        return 1;
    return ntos_CloseHandle(h);
}
int ReadFile(void *h, void *b, u32 n, u32 *done, void *ov)
{
    if ((u32)h != 0x70000001u)
        return ntos_ReadFile(h, b, n, done, ov);
    u32 i = 0;
    while (i < n && input_script[ce_input]) {
        char ch = input_script[ce_input++];
        ((char *)b)[i++] = ch;
        if (ch == '\n')
            break;
    }
    if (done)
        *done = i;
    return 1;
}
int WriteFile(void *h, const void *b, u32 n, u32 *done, void *ov)
{
    if ((u32)h != 0x70000001u)
        return ntos_WriteFile(h, b, n, done, ov);
    char s[256];
    u32 at = 0;
    while (at < n) {
        unsigned i = 0;
        while (i < 255 && at < n)
            s[i++] = ((const char *)b)[at++];
        s[i] = 0;
        coredll_puts(s);
    }
    if (done)
        *done = n;
    return 1;
}
u32 Sleep(u32 ms)
{
    (void)ms;
    return 0;
}
u32 PPSHRestart(void)
{
    return 1;
}
u32 SignalStarted(u32 id)
{
    (void)id;
    return 1;
}
u32 OpenEventW(u32 access, u32 inherit, const u16 *name)
{
    (void)access;
    (void)inherit;
    (void)name;
    return 0x70000002u;
}
u32 WaitForSingleObject(u32 h, u32 ms)
{
    (void)ms;
    return h == 0x70000002u ? 0 : 0xffffffffu;
}
u32 EventModify(u32 h, u32 op)
{
    (void)h;
    (void)op;
    return 1;
}
u32 CeSetThreadPriority(u32 h, u32 p)
{
    (void)h;
    (void)p;
    return 1;
}
u32 CeGetThreadPriority(u32 h)
{
    (void)h;
    return 128;
}
u32 KernelLibIoControl(u32 h, u32 op, void *in, u32 ins, void *out, u32 outs, u32 *done)
{
    (void)h;
    (void)op;
    (void)in;
    (void)ins;
    if (done)
        *done = 0;
    if (out && outs)
        for (u32 i = 0; i < outs; ++i)
            ((char *)out)[i] = 0;
    return 0;
}
u32 RegOpenKeyExW(u32 h, const u16 *n, u32 opt, u32 access, u32 *out)
{
    (void)h;
    (void)n;
    (void)opt;
    (void)access;
    if (out)
        *out = 0;
    return 2;
}
u32 RegQueryValueExW(u32 h, const u16 *n, void *reserved, u32 *type, void *out, u32 *nbytes)
{
    (void)h;
    (void)n;
    (void)reserved;
    (void)type;
    (void)out;
    (void)nbytes;
    return 2;
}
u32 RegCloseKey(u32 h)
{
    (void)h;
    return 0;
}
u32 wcscmp(const u16 *a, const u16 *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (int)*a - (int)*b;
}
u32 wcsncmp(const u16 *a, const u16 *b, u32 n)
{
    while (n && *a && *a == *b) {
        --n;
        ++a;
        ++b;
    }
    return n ? (int)*a - (int)*b : 0;
}
u32 wcslen(const u16 *a)
{
    u32 n = 0;
    while (a[n])
        ++n;
    return n;
}
u16 *wcscpy(u16 *d, const u16 *s)
{
    u16 *p = d;
    while ((*d++ = *s++)) {
    }
    return p;
}
u16 *wcsncpy(u16 *d, const u16 *s, u32 n)
{
    u16 *p = d;
    while (n--) {
        *d++ = *s;
        if (*s)
            ++s;
    }
    return p;
}
u16 *wcsncat(u16 *d, const u16 *s, u32 n)
{
    u16 *p = d;
    while (*d)
        ++d;
    while (n-- && *s)
        *d++ = *s++;
    *d = 0;
    return p;
}
u16 *wcscat(u16 *d, const u16 *s)
{
    return wcsncat(d, s, 0x7fffffffu);
}
u32 wcstombs(char *d, const u16 *s, u32 n)
{
    u32 i = 0;
    while (i < n && s[i]) {
        d[i] = s[i] < 128 ? (char)s[i] : '?';
        ++i;
    }
    if (i < n)
        d[i] = 0;
    return i;
}
u32 iswctype(u32 ch, u32 mask)
{
    u32 c = 0;
    if (ch >= '0' && ch <= '9')
        c |= 4;
    if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t')
        c |= 8;
    if (ch >= 'A' && ch <= 'Z')
        c |= 1;
    if (ch >= 'a' && ch <= 'z')
        c |= 2;
    return c & mask;
}
u32 MultiByteToWideChar(u32 cp, u32 flags, const char *s, int n, u16 *d, int capacity)
{
    (void)cp;
    (void)flags;
    int i = 0;
    if (n < 0) {
        n = 0;
        while (s[n++]) {
        }
    }
    if (!d)
        return n;
    while (i < n && i < capacity) {
        d[i] = (unsigned char)s[i];
        ++i;
    }
    return i;
}
u32 _wcsicmp(const u16 *a, const u16 *b)
{
    u32 x, y;
    do {
        x = *a++;
        y = *b++;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return (int)x - (int)y;
    } while (x);
    return 0;
}
u16 *wcsrchr(const u16 *p, u32 c)
{
    const u16 *last = 0;
    do {
        if (*p == c)
            last = p;
    } while (*p++);
    return (u16 *)last;
}
u16 *wcsstr(const u16 *p, const u16 *needle)
{
    u32 n = wcslen(needle);
    for (; *p; ++p)
        if (!wcsncmp(p, needle, n))
            return (u16 *)p;
    return n ? 0 : (u16 *)p;
}
u32 _wtol(const u16 *p)
{
    u32 n = 0;
    while (*p >= '0' && *p <= '9')
        n = n * 10 + *p++ - '0';
    return n;
}
void CeCoreDetach(void);
int main(void *instance, u32 reason, void *reserved)
{
    (void)instance;
    (void)reserved;
    if (reason == 0) {
        CeCoreDetach();
        return 1;
    }
    legacy_nav = 0;
    legacy_nav_loaded = 1;
    return 1;
}
u16 *wcstok(u16 *s, const u16 *delimiters)
{
    static u16 *next;
    u16 *p, *d;
    if (!s)
        s = next;
    if (!s)
        return 0;
    while (*s) {
        for (d = (u16 *)delimiters; *d && *d != *s; ++d) {
        }
        if (!*d)
            break;
        ++s;
    }
    if (!*s) {
        next = 0;
        return 0;
    }
    p = s;
    while (*s) {
        for (d = (u16 *)delimiters; *d && *d != *s; ++d) {
        }
        if (*d) {
            *s++ = 0;
            next = s;
            return p;
        }
        ++s;
    }
    next = 0;
    return p;
}

void *FindWindowW(const u16 *class_name, const u16 *title)
{
    if (!g_window.magic)
        return 0;
    if (class_name && !same_wstr(class_name, g_class_name))
        return 0;
    if (title && !same_wstr(title, g_window.title))
        return 0;
    return &g_window;
}

#include "coredll/services.c"
#include "coredll/menus_images.c"
#include "coredll/format.c"

#include "coredll/windows.c"
/* Called after dependent DLLs detach, matching NTOS' reverse attach order. */
void CeCoreDetach(void)
{
    while (ce_modules)
        FreeLibrary(ce_modules->image);
    while (ce_resource_strings) {
        CE_RESOURCE_STRING *p = ce_resource_strings;
        ce_resource_strings = p->next;
        ce_free(p);
    }
    while (ce_images)
        ImageList_Destroy(ce_images);
    CeControlsReset();
    while (ce_locals)
        LocalFree(ce_locals + 1);
    for (unsigned i = 0; i < 32; ++i)
        if (nt_files[i].used)
            ntos_CloseHandle((void *)(NT_HANDLE_TAG + i + 1));
    for (unsigned i = 0; i < 8; ++i)
        if (nt_finds[i].used)
            FindClose((void *)(0x30000u + i + 1));
    g_window.magic = 0;
    g_window.proc = 0;
    g_class_proc = 0;
    g_qhead = g_qtail = 0;
    ce_focus = 0;
    ce_main_user = 0;
    ce_failed = 0;
    ce_input = 0;
}

#include "common/msvc_callbacks.c"
