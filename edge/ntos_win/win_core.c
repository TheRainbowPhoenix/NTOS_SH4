/* win_core.c — NTOS_SH4_windows: builtin COREDLL + window manager + LCD.
 *
 * Win32 twin of edge/ntos_linux/host_core.c: the exact same WinCE-style API
 * surface and semantics (single device window, CE message queue, GDI fills
 * into the 320x528 framebuffer, \fls0-rooted file layer, SIP), implemented
 * on native Win32 so the same x86-64 PE payload DLLs run unchanged on a
 * real Windows desktop.
 *
 * Extra vs the Linux host: an interactive mode. When no e2e script is
 * driving the app, get_message_w pumps real Win32 input (a 640x1056 window
 * showing the device LCD) so the payload can be clicked, dragged and typed
 * at by hand — the fast iteration loop for uXAML/svf debugging.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "ntos_linux.h"
#include "win_host.h"

U32 *host_fb;
static int fb_dirty_flag;
static int quit_posted;

static HostMsg *msg_head, *msg_tail;
static int (*idle_hook)(void);
static HostWndClass wndclass;
static void *cur_hwnd;
static int cur_hwnd_serial = 0x10001;
static int invalidated;
static int sip_visible;
static const char *g_fs_root = ".";
static char log_buf[4096];
static FILE *g_logfile;

/* interactive state (win_host.c owns the window) */
int win_interactive;
int win_pump(void); /* provided by win_host.c window layer */

/* ---------------- logging: console + file ---------------- */
void host_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log_buf, sizeof(log_buf), fmt, ap);
    va_end(ap);
    fputs("[ntos] ", stdout);
    fputs(log_buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (g_logfile) {
        fputs("[ntos] ", g_logfile);
        fputs(log_buf, g_logfile);
        fputc('\n', g_logfile);
        fflush(g_logfile);
    }
}

void host_log_set_file(FILE *f)
{
    g_logfile = f;
}

int host_fb_dirty(void)
{
    return fb_dirty_flag;
}

void host_post(U32 message, U32 wp, U32 lp)
{
    HostMsg *m = calloc(1, sizeof(*m));
    m->hwnd = cur_hwnd;
    m->message = message;
    m->wparam = wp;
    m->lparam = lp;
    m->time = GetTickCount();
    if (msg_tail)
        msg_tail->next = m;
    else
        msg_head = m;
    msg_tail = m;
}

void host_set_idle_hook(int (*hook)(void))
{
    idle_hook = hook;
}

/* ---------------- foreground app launch (CreateProcessW model) ---------------- */
static int (*launch_hook)(const char *image, const char *cmdline);

void host_set_launch_hook(int (*hook)(const char *, const char *))
{
    launch_hook = hook;
}

static W16 launch_cmdline[512];

static MSABI int create_process_w(const W16 *image, const W16 *cmdline, void *psa, void *pta,
                                  int inherit, U32 flags, void *env, const W16 *cwd, void *si,
                                  void *pi)
{
    (void)psa;
    (void)pta;
    (void)inherit;
    (void)flags;
    (void)env;
    (void)cwd;
    (void)si;
    (void)pi;
    char img[256] = "", cmd[512] = "";
    size_t j = 0;
    for (size_t i = 0; image && image[i] && j + 1 < sizeof(img); ++i)
        if (image[i] < 0x80)
            img[j++] = (char)image[i];
    img[j] = 0;
    j = 0;
    for (size_t i = 0; cmdline && cmdline[i] && j + 1 < sizeof(cmd); ++i)
        if (cmdline[i] < 0x80)
            cmd[j++] = (char)cmdline[i];
    cmd[j] = 0;
    if (!launch_hook)
        return 0;
    return launch_hook(img, cmd);
}

static MSABI const W16 *get_command_line_w(void)
{
    return launch_cmdline;
}

void host_set_launch_cmdline(const char *cmdline)
{
    size_t i = 0;
    while (cmdline && cmdline[i] && i + 1 < sizeof(launch_cmdline) / sizeof(launch_cmdline[0])) {
        launch_cmdline[i] = (W16)(unsigned char)cmdline[i];
        ++i;
    }
    launch_cmdline[i] = 0;
}

void host_sip_set(int visible)
{
    sip_visible = !!visible;
}

int host_sip_visible(void)
{
    return sip_visible;
}

void host_request_quit(void)
{
    quit_posted = 1;
}

void host_init(int argc, char **argv, const char *fs_root)
{
    (void)argc;
    (void)argv;
    g_fs_root = fs_root;
    host_fb = calloc(NTOS_SCREEN_W * NTOS_SCREEN_H, sizeof(U32));
    /* device boots to the light theme background (phone.h SURFACE) */
    for (int i = 0; i < NTOS_SCREEN_W * NTOS_SCREEN_H; ++i)
        host_fb[i] = 0x00F2 | 0xF7 << 8 | 0xFB << 16; /* 0x00BBGGRR = F2 F7 FB */
}

void host_shutdown(void)
{
    free(host_fb);
    host_fb = NULL;
}

/* ---------------- W16 (UTF-16) helpers ---------------- */
static size_t w16len(const W16 *s)
{
    size_t n = 0;
    while (s && s[n])
        ++n;
    return n;
}

/* Convert a device path (\fls0\x or C:\...) to a host path under fs_root.
 * Win32 path APIs accept forward slashes; keep the join simple. */
static void devpath_to_host(const W16 *w, char *out, size_t cap)
{
    char tmp[1024];
    size_t n = w16len(w), j = 0;
    for (size_t i = 0; i < n && j + 1 < sizeof(tmp); ++i) {
        unsigned c = w[i];
        if (c == '\\')
            tmp[j++] = '/';
        else if (c < 0x80)
            tmp[j++] = (char)c;
        else
            tmp[j++] = '?';
    }
    tmp[j] = 0;
    while (j && (tmp[0] == '/'))
        memmove(tmp, tmp + 1, j--); /* strip leading slashes */
    snprintf(out, cap, "%s/%s", g_fs_root, tmp);
}

/* ---------------- window manager + message loop ---------------- */
static MSABI int def_wndproc(void *hwnd, U32 msg, U32 wp, U32 lp)
{
    (void)hwnd;
    (void)msg;
    (void)wp;
    (void)lp;
    return 0;
}

static MSABI U32 register_class_w(const HostWndClass *wc)
{
    if (!wc || !wc->proc) {
        wndclass.proc = def_wndproc;
        return 1;
    }
    wndclass = *wc;
    return 1;
}

static MSABI void *create_window_ex_w(U32 ex, const W16 *cls, const W16 *title, U32 style, int x,
                                      int y, int w, int h, void *parent, void *menu, void *inst,
                                      void *param)
{
    (void)ex;
    (void)cls;
    (void)title;
    (void)style;
    (void)parent;
    (void)menu;
    (void)inst;
    (void)param;
    cur_hwnd = (void *)(uintptr_t)++cur_hwnd_serial;
    if (wndclass.proc)
        wndclass.proc(cur_hwnd, WM_CREATE_, 0, 0);
    /* a fresh window starts fully invalid so the first UpdateWindow paints */
    invalidated = 1;
    return cur_hwnd;
}

static MSABI int show_window(void *hwnd, int cmd)
{
    (void)hwnd;
    (void)cmd;
    return 1;
}

static MSABI int update_window(void *hwnd)
{
    /* synchronous WM_PAINT like WinCE */
    if (hwnd == cur_hwnd && wndclass.proc && invalidated) {
        invalidated = 0;
        wndclass.proc(hwnd, WM_PAINT_, 0, 0);
        fb_dirty_flag = 1;
        win_frame_ready(); /* blit to the interactive window if present */
    }
    return 1;
}

static MSABI int invalidate_rect(void *hwnd, const Rect *r, int erase)
{
    (void)hwnd;
    (void)r;
    (void)erase;
    invalidated = 1;
    return 1;
}

static MSABI int destroy_window(void *hwnd)
{
    if (hwnd == cur_hwnd && wndclass.proc)
        wndclass.proc(hwnd, WM_DESTROY_, 0, 0);
    cur_hwnd = NULL;
    return 1;
}

static MSABI void post_quit_message(int code)
{
    (void)code;
    quit_posted = 1;
}

static MSABI int get_message_w(HostMsg *out, void *hwnd, U32 min, U32 max)
{
    (void)hwnd;
    (void)min;
    (void)max;
    for (;;) {
        if (msg_head) {
            HostMsg *m = msg_head;
            msg_head = m->next;
            if (!msg_head)
                msg_tail = NULL;
            *out = *m;
            U32 msg = m->message;
            free(m);
            if (msg == WM_QUIT_) {
                quit_posted = 1;
                continue;
            }
            return 1;
        }
        if (quit_posted)
            return 0;
        /* let the e2e script feed the next interaction step */
        if (idle_hook && idle_hook())
            continue;
        if (quit_posted)
            return 0;
        if (invalidated && cur_hwnd) {
            /* WinCE generates WM_PAINT on demand when the update region is
             * non-empty and the queue is idle */
            host_post(WM_PAINT_, 0, 0);
            continue;
        }
        if (win_interactive) {
            /* real desktop input (mouse/keyboard) lands in the CE queue
             * from the window proc; pump until something happens */
            if (win_pump())
                continue;
            if (quit_posted)
                return 0;
            continue;
        }
        /* nothing left to do: deterministic exit for scripted e2e runs */
        quit_posted = 1;
        return 0;
    }
}

static MSABI int translate_message(const HostMsg *m)
{
    (void)m;
    return 0;
}

static MSABI int dispatch_message_w(const HostMsg *m)
{
    if (wndclass.proc && m->hwnd == cur_hwnd)
        return wndclass.proc(m->hwnd, m->message, m->wparam, m->lparam);
    return 0;
}

/* ---------------- GDI ---------------- */
static MSABI HostBrush *create_solid_brush(U32 color)
{
    HostBrush *b = malloc(sizeof(*b));
    b->color = color;
    return b;
}

static MSABI int fill_rect(void *hdc, const Rect *r, HostBrush *b)
{
    (void)hdc;
    if (!r || !b)
        return 0;
    int x0 = r->left < 0 ? 0 : r->left;
    int y0 = r->top < 0 ? 0 : r->top;
    int x1 = r->right > NTOS_SCREEN_W ? NTOS_SCREEN_W : r->right;
    int y1 = r->bottom > NTOS_SCREEN_H ? NTOS_SCREEN_H : r->bottom;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            host_fb[y * NTOS_SCREEN_W + x] = b->color;
    fb_dirty_flag = 1;
    return 1;
}

static MSABI int delete_object(void *obj)
{
    free(obj);
    return 1;
}

static MSABI void *begin_paint(void *hwnd, HostPaint *ps)
{
    memset(ps, 0, sizeof(*ps));
    ps->hdc = (void *)(uintptr_t)0xD00D;
    ps->erase = 1;
    ps->area.left = 0;
    ps->area.top = 0;
    ps->area.right = NTOS_SCREEN_W;
    ps->area.bottom = NTOS_SCREEN_H;
    return ps->hdc;
}

static MSABI int end_paint(void *hwnd, const HostPaint *ps)
{
    (void)hwnd;
    (void)ps;
    fb_dirty_flag = 1;
    win_frame_ready();
    return 1;
}

/* ---------------- SIP ---------------- */
static MSABI int sip_show_im(U32 flags)
{
    sip_visible = !!flags;
    return 1;
}

static MSABI int sip_get_info(HostSipInfo *info)
{
    if (!info || info->size < sizeof(*info))
        return 0;
    info->flags = sip_visible ? 1u : 0u;
    info->desktop.left = 0;
    info->desktop.top = 0;
    info->desktop.right = NTOS_SCREEN_W;
    info->desktop.bottom = NTOS_SCREEN_H;
    info->sip.left = 0;
    info->sip.top = 271;
    info->sip.right = NTOS_SCREEN_W;
    info->sip.bottom = NTOS_SCREEN_H;
    return 1;
}

/* ---------------- files (\fls0\ rooted at host dir) ---------------- */
typedef struct {
    HANDLE h;    /* file handle (INVALID when this is a find handle) */
    HANDLE find; /* FindFirstFileW handle for enumeration */
    int is_find;
    char base[1100];
} HostFile;

static void char_to_wide(const char *path, wchar_t *out, size_t cap)
{
    size_t k = 0;
    for (; path[k] && k + 1 < cap; ++k)
        out[k] = (unsigned char)path[k];
    out[k] = 0;
}

static MSABI void *create_file_w(const W16 *name, U32 access, U32 share, void *sa, U32 creation,
                                 U32 flags, void *tmpl)
{
    (void)share;
    (void)sa;
    (void)flags;
    (void)tmpl;
    char path[1100];
    devpath_to_host(name, path, sizeof(path));
    wchar_t wpath[1200];
    char_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0]));

    DWORD dw_access = 0;
    if (access & 0x40000000u) /* GENERIC_WRITE */
        dw_access |= GENERIC_WRITE | GENERIC_READ;
    else
        dw_access = GENERIC_READ;
    DWORD dw_create = (creation == 4) ? OPEN_ALWAYS : /* OPEN_ALWAYS (append semantics) */
                          (creation == 2) ? CREATE_ALWAYS
                                          : /* CREATE_ALWAYS (trunc) */
                          (creation == 3) ? OPEN_EXISTING
                                          : /* OPEN_EXISTING */
                          (creation == 5) ? TRUNCATE_EXISTING
                                          : CREATE_NEW;
    HANDLE h = CreateFileW(wpath, dw_access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, dw_create,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return (void *)(intptr_t)-1; /* INVALID_HANDLE_VALUE (sign-extended) */
    HostFile *hf = calloc(1, sizeof(*hf));
    hf->h = h;
    snprintf(hf->base, sizeof(hf->base), "%s", path);
    return hf;
}

static MSABI int read_file(void *h, void *buf, U32 want, U32 *got)
{
    HostFile *hf = h;
    if (!hf || hf->is_find || hf->h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD n = 0;
    if (!ReadFile(hf->h, buf, want, &n, NULL))
        return 0;
    if (got)
        *got = n;
    return 1;
}

static MSABI int write_file(void *h, const void *buf, U32 want, U32 *written)
{
    HostFile *hf = h;
    if (!hf || hf->is_find || hf->h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD n = 0;
    if (!WriteFile(hf->h, buf, want, &n, NULL))
        return 0;
    if (written)
        *written = n;
    return n == want;
}

static MSABI int close_handle(void *h)
{
    HostFile *hf = h;
    if (!hf)
        return 0;
    if (hf->is_find && hf->find != INVALID_HANDLE_VALUE)
        FindClose(hf->find);
    if (!hf->is_find && hf->h != INVALID_HANDLE_VALUE)
        CloseHandle(hf->h);
    free(hf);
    return 1;
}

static MSABI U32 get_file_size(void *h, U32 *high)
{
    HostFile *hf = h;
    if (!hf || hf->is_find || hf->h == INVALID_HANDLE_VALUE)
        return 0xFFFFFFFFu;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(hf->h, &sz))
        return 0xFFFFFFFFu;
    if (high)
        *high = (U32)(sz.QuadPart >> 32);
    return (U32)sz.QuadPart;
}

static MSABI U32 set_file_pointer(void *h, int dist, U32 *high, U32 method)
{
    HostFile *hf = h;
    (void)high;
    if (!hf || hf->is_find || hf->h == INVALID_HANDLE_VALUE)
        return 0xFFFFFFFFu;
    DWORD m = (method == 0) ? FILE_BEGIN : (method == 1) ? FILE_CURRENT : FILE_END;
    DWORD at = SetFilePointer(hf->h, dist, NULL, m);
    return (at == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) ? 0xFFFFFFFFu : at;
}

/* WinCE FIND_DATA (payload/phone/phone_io.h + payload/control.c layout):
 * {u32 attrs; u32 times[6] (creation/access/write FILETIMEs); u32 size_hi,
 *  size_lo, oid; W16 name[260]} = 560 bytes. The phone payloads and the
 *  device COREDLL share it, so the host must too. */
typedef struct {
    unsigned attrs;
    unsigned times[6];
    unsigned size_hi, size_lo, oid;
    unsigned short name[260];
} WinceFindData;

static void fill_wince_find_from_win32(WinceFindData *out, const WIN32_FIND_DATAW *fd)
{
    memset(out, 0, sizeof(*out));
    out->attrs = (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 16u : 0u;
    out->size_lo = (unsigned)fd->nFileSizeLow;
    out->size_hi = (unsigned)fd->nFileSizeHigh;
    /* FILETIMEs are already 100ns ticks since 1601: creation/access/write */
    out->times[0] = (unsigned)fd->ftCreationTime.dwLowDateTime;
    out->times[1] = (unsigned)fd->ftCreationTime.dwHighDateTime;
    out->times[2] = (unsigned)fd->ftLastAccessTime.dwLowDateTime;
    out->times[3] = (unsigned)fd->ftLastAccessTime.dwHighDateTime;
    out->times[4] = (unsigned)fd->ftLastWriteTime.dwLowDateTime;
    out->times[5] = (unsigned)fd->ftLastWriteTime.dwHighDateTime;
    for (int i = 0; i < 259 && fd->cFileName[i]; ++i)
        out->name[i] = fd->cFileName[i];
    out->name[259] = 0;
}

static MSABI intptr_t find_first_file_w(const W16 *pattern, WinceFindData *out)
{
    char path[1100];
    devpath_to_host(pattern, path, sizeof(path));
    size_t n = strlen(path);
    if (n && path[n - 1] == '*')
        path[n - 1] = 0; /* POSIX opendir(dirname(pattern)) model */

    char dirpart[1100];
    snprintf(dirpart, sizeof(dirpart), "%s", path);
    char *slash = strrchr(dirpart, '/');
    const char *leaf = "";
    if (slash) {
        *slash = 0;
        leaf = slash + 1;
    } else {
        dirpart[0] = 0;
        leaf = path;
    }
    char wild[1300];
    if (dirpart[0])
        snprintf(wild, sizeof(wild), "%s/%s*", dirpart, leaf);
    else
        snprintf(wild, sizeof(wild), "%s*", leaf[0] ? leaf : "");

    wchar_t wwild[1400];
    char_to_wide(wild, wwild, sizeof(wwild) / sizeof(wwild[0]));

    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(wwild, &fd);
    if (find == INVALID_HANDLE_VALUE)
        return (intptr_t)(uintptr_t)0xFFFFFFFFu;
    /* skip the dots (WinCE FindFirst never returns them) */
    while (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
        if (!FindNextFileW(find, &fd)) {
            FindClose(find);
            return (intptr_t)(uintptr_t)0xFFFFFFFFu;
        }
    }
    fill_wince_find_from_win32(out, &fd);
    HostFile *hf = calloc(1, sizeof(*hf));
    hf->is_find = 1;
    hf->find = find;
    snprintf(hf->base, sizeof(hf->base), "%s", dirpart[0] ? dirpart : ".");
    return (intptr_t)hf;
}

static MSABI int find_next_file_w(intptr_t h, WinceFindData *out)
{
    HostFile *hf = (HostFile *)h;
    if (!hf || !hf->is_find || hf->find == INVALID_HANDLE_VALUE)
        return 0;
    WIN32_FIND_DATAW fd;
    for (;;) {
        if (!FindNextFileW(hf->find, &fd))
            return 0;
        if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
            break;
    }
    fill_wince_find_from_win32(out, &fd);
    return 1;
}

static MSABI int find_close(intptr_t h)
{
    HostFile *hf = (HostFile *)h;
    if (!hf)
        return 0;
    if (hf->is_find && hf->find != INVALID_HANDLE_VALUE)
        FindClose(hf->find);
    free(hf);
    return 1;
}

static MSABI int delete_file_w(const W16 *name)
{
    char path[1100];
    devpath_to_host(name, path, sizeof(path));
    wchar_t wpath[1200];
    char_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0]));
    return DeleteFileW(wpath);
}

static MSABI int create_directory_w(const W16 *name, void *sa)
{
    (void)sa;
    char path[1100];
    devpath_to_host(name, path, sizeof(path));
    wchar_t wpath[1200];
    char_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0]));
    return CreateDirectoryW(wpath, NULL);
}

static MSABI int remove_directory_w(const W16 *name)
{
    char path[1100];
    devpath_to_host(name, path, sizeof(path));
    wchar_t wpath[1200];
    char_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0]));
    return RemoveDirectoryW(wpath);
}

static MSABI int move_file_w(const W16 *from, const W16 *to)
{
    char a[1100], b[1100];
    devpath_to_host(from, a, sizeof(a));
    devpath_to_host(to, b, sizeof(b));
    wchar_t wa[1200], wb[1200];
    char_to_wide(a, wa, sizeof(wa) / sizeof(wa[0]));
    char_to_wide(b, wb, sizeof(wb) / sizeof(wb[0]));
    return MoveFileW(wa, wb);
}

static MSABI int copy_file_w(const W16 *from, const W16 *to, int fail_if_exists)
{
    char a[1100], b[1100];
    devpath_to_host(from, a, sizeof(a));
    devpath_to_host(to, b, sizeof(b));
    wchar_t wa[1200], wb[1200];
    char_to_wide(a, wa, sizeof(wa) / sizeof(wa[0]));
    char_to_wide(b, wb, sizeof(wb) / sizeof(wb[0]));
    return CopyFileW(wa, wb, !!fail_if_exists);
}

static MSABI int get_disk_free_space_ex_w(const W16 *path, U64 *free_avail, U64 *total,
                                          U64 *free_total)
{
    char p[1100];
    devpath_to_host(path, p, sizeof(p));
    wchar_t wp[1200];
    char_to_wide(p, wp, sizeof(wp) / sizeof(wp[0]));
    ULARGE_INTEGER fa, tt, ft;
    if (!GetDiskFreeSpaceExW(wp[0] ? wp : L".", &fa, &tt, &ft))
        return 0;
    if (free_avail)
        *free_avail = fa.QuadPart;
    if (total)
        *total = tt.QuadPart;
    if (free_total)
        *free_total = ft.QuadPart;
    return 1;
}

/* ---------------- misc coredll ---------------- */
static MSABI void coredll_puts(const char *s)
{
    puts(s);
    fflush(stdout);
}

static MSABI int coredll_launch(const char *path)
{
    host_log("coredll_launch(\"%s\") requested (host: single-process, ignored)", path);
    return 0;
}

static MSABI void sleep_ms(U32 ms)
{
    Sleep(ms);
}

static MSABI U32 get_tick_count(void)
{
    return GetTickCount();
}

static MSABI void *gc_alloc(size_t n)
{
    return malloc(n);
}
static MSABI void gc_free(void *p)
{
    free(p);
}
static MSABI void *gc_memcpy(void *d, const void *s, size_t n)
{
    return memcpy(d, s, n);
}
static MSABI void *gc_memset(void *d, int c, size_t n)
{
    return memset(d, c, n);
}
static MSABI void *gc_memmove(void *d, const void *s, size_t n)
{
    return memmove(d, s, n);
}

/* ---------------- builtin table (name -> Win64-ABI fn) ----------------
 * Registered under the REAL WinCE/coredll API names so payload sources use
 * the exact names the device COREDLL exports. Identical list to the Linux
 * host: payloads must not be able to tell the two hosts apart. */
#define B(n) {#n, (void *)(uintptr_t)n}
#define B2(real, fn) {#real, (void *)(uintptr_t)fn}
const BuiltinExport host_builtins[] = {
    B2(RegisterClassW, register_class_w),
    B2(CreateWindowExW, create_window_ex_w),
    B2(ShowWindow, show_window),
    B2(UpdateWindow, update_window),
    B2(InvalidateRect, invalidate_rect),
    B2(DestroyWindow, destroy_window),
    B2(PostQuitMessage, post_quit_message),
    B2(GetMessageW, get_message_w),
    B2(TranslateMessage, translate_message),
    B2(DispatchMessageW, dispatch_message_w),
    B2(CreateSolidBrush, create_solid_brush),
    B2(FillRect, fill_rect),
    B2(DeleteObject, delete_object),
    B2(BeginPaint, begin_paint),
    B2(EndPaint, end_paint),
    B2(SipShowIM, sip_show_im),
    B2(SipGetInfo, sip_get_info),
    B2(CreateFileW, create_file_w),
    B2(ReadFile, read_file),
    B2(WriteFile, write_file),
    B2(CloseHandle, close_handle),
    B2(GetFileSize, get_file_size),
    B2(SetFilePointer, set_file_pointer),
    B2(FindFirstFileW, find_first_file_w),
    B2(FindNextFileW, find_next_file_w),
    B2(FindClose, find_close),
    B2(DeleteFileW, delete_file_w),
    B2(CreateDirectoryW, create_directory_w),
    B2(RemoveDirectoryW, remove_directory_w),
    B2(MoveFileW, move_file_w),
    B2(CopyFileW, copy_file_w),
    B2(GetDiskFreeSpaceExW, get_disk_free_space_ex_w),
    B(coredll_puts),
    B(coredll_launch),
    B2(CreateProcessW, create_process_w),
    B2(GetCommandLineW, get_command_line_w),
    B2(Sleep, sleep_ms),
    B2(GetTickCount, get_tick_count),
    /* freestanding payloads pull these through msvcrt */
    {"malloc", (void *)(uintptr_t)gc_alloc},
    {"free", (void *)(uintptr_t)gc_free},
    {"memcpy", (void *)(uintptr_t)gc_memcpy},
    {"memset", (void *)(uintptr_t)gc_memset},
    {"memmove", (void *)(uintptr_t)gc_memmove},
};
const size_t host_builtin_count = sizeof(host_builtins) / sizeof(host_builtins[0]);

void *host_lookup_builtin(const char *name)
{
    for (size_t i = 0; i < host_builtin_count; ++i)
        if (!strcmp(host_builtins[i].name, name))
            return host_builtins[i].fn;
    return NULL;
}
