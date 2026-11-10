/* host_core.c — NTOS_SH4_linux builtin COREDLL + window manager + LCD.
 *
 * Implements the WinCE-style API surface the NTOS phone payloads use
 * (payload/phone/*.c): window class registration, a single device window,
 * the message loop, GDI fills, the soft-input panel and the coredll file
 * layer rooted at a host directory that plays \fls0\.
 *
 * Every exported function is MSABI: the payloads are Win64-ABI PE images.
 */
#define _DEFAULT_SOURCE
#include "ntos_linux.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

U32 *host_fb;
static int fb_dirty_flag;
static int quit_posted;
static int pumps_left = -1;

static HostMsg *msg_head, *msg_tail;
static int (*idle_hook)(void);
static HostWndClass wndclass;
static void *cur_hwnd;
static int cur_hwnd_serial = 0x10001;
static int invalidated;
static int sip_visible;
static const char *g_fs_root = ".";
static char log_buf[4096];

/* ---------------- misc helpers ---------------- */
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
    m->time = (U32)time(NULL);
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

/* Convert a device path (\fls0\x or C:\...) to a host path under fs_root. */
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

static MSABI void *create_window_ex_w(U32 ex, const W16 *cls, const W16 *title, U32 style,
                                      int x, int y, int w, int h, void *parent, void *menu,
                                      void *inst, void *param)
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
    FILE *f;
    DIR *d;
    int is_dir;
    char base[1100];
} HostFile;

static MSABI void *create_file_w(const W16 *name, U32 access, U32 share, void *sa,
                                 U32 creation, U32 flags, void *tmpl)
{
    (void)share;
    (void)sa;
    (void)flags;
    (void)tmpl;
    char path[1100];
    devpath_to_host(name, path, sizeof(path));
    HostFile *hf = calloc(1, sizeof(*hf));
    if (access & 0x40000000u /* GENERIC_WRITE */) {
        const char *mode = (creation == 4 /*OPEN_ALWAYS*/) ? "ab+" : (creation == 2 /*CREATE_ALWAYS*/) ? "wb" : "r+b";
        hf->f = fopen(path, mode);
        if (!hf->f && (creation == 4 || creation == 2)) {
            hf->f = fopen(path, mode[0] == 'a' ? "w+b" : "wb");
            if (hf->f && creation == 4)
                fseek(hf->f, 0, SEEK_END);
        }
    } else {
        hf->f = fopen(path, "rb");
    }
    if (!hf->f) {
        free(hf);
        return (void *)(uintptr_t)0xFFFFFFFF; /* INVALID_HANDLE_VALUE */
    }
    return hf;
}

static MSABI int read_file(void *h, void *buf, U32 want, U32 *got)
{
    HostFile *hf = h;
    if (!hf || !hf->f)
        return 0;
    size_t n = fread(buf, 1, want, hf->f);
    if (got)
        *got = (U32)n;
    return 1;
}

static MSABI int write_file(void *h, const void *buf, U32 want, U32 *written)
{
    HostFile *hf = h;
    if (!hf || !hf->f)
        return 0;
    size_t n = fwrite(buf, 1, want, hf->f);
    if (written)
        *written = (U32)n;
    return n == want;
}

static MSABI int close_handle(void *h)
{
    HostFile *hf = h;
    if (!hf)
    return 0;
    if (hf->f)
        fclose(hf->f);
    if (hf->d)
        closedir(hf->d);
    free(hf);
    return 1;
}

static MSABI U32 get_file_size(void *h, U32 *high)
{
    HostFile *hf = h;
    if (high)
        *high = 0;
    if (!hf || !hf->f)
        return 0xFFFFFFFFu;
    long cur = ftell(hf->f);
    fseek(hf->f, 0, SEEK_END);
    long sz = ftell(hf->f);
    fseek(hf->f, cur, SEEK_SET);
    return (U32)sz;
}

static MSABI U32 set_file_pointer(void *h, int dist, U32 *high, U32 method)
{
    HostFile *hf = h;
    (void)high;
    if (!hf || !hf->f)
        return 0xFFFFFFFFu;
    fseek(hf->f, dist, method == 0 ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END);
    return (U32)ftell(hf->f);
}

typedef struct {
    unsigned attrib;
    unsigned long size;
    char name[260];
} HostFindData;

static MSABI intptr_t find_first_file_w(const W16 *pattern, HostFindData *out)
{
    char path[1100];
    devpath_to_host(pattern, path, sizeof(path));
    size_t n = strlen(path);
    if (n && path[n - 1] == '*')
        path[n - 1] = 0;
    HostFile *hf = calloc(1, sizeof(*hf));
    snprintf(hf->base, sizeof(hf->base), "%s", path);
    hf->d = opendir(path[0] ? path : ".");
    if (!hf->d) {
        free(hf);
        return -1;
    }
    struct dirent *de = readdir(hf->d);
    if (!de) {
        closedir(hf->d);
        free(hf);
        return -1;
    }
    out->attrib = de->d_type == DT_DIR ? 16u : 0u;
    snprintf(out->name, sizeof(out->name), "%s", de->d_name);
    return (intptr_t)hf;
}

static MSABI int find_next_file_w(intptr_t h, HostFindData *out)
{
    HostFile *hf = (HostFile *)h;
    if (!hf || !hf->d)
        return 0;
    struct dirent *de = readdir(hf->d);
    if (!de)
        return 0;
    out->attrib = de->d_type == DT_DIR ? 16u : 0u;
    snprintf(out->name, sizeof(out->name), "%s", de->d_name);
    return 1;
}

static MSABI int find_close(intptr_t h)
{
    HostFile *hf = (HostFile *)h;
    if (!hf)
        return 0;
    if (hf->d)
        closedir(hf->d);
    free(hf);
    return 1;
}

static MSABI int delete_file_w(const W16 *name)
{
    char path[1100];
    devpath_to_host(name, path, sizeof(path));
    return unlink(path) == 0;
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
    struct timespec ts = {(time_t)ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static MSABI U32 get_tick_count(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static MSABI void *gc_alloc(size_t n) { return malloc(n); }
static MSABI void gc_free(void *p) { free(p); }
static MSABI void *gc_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
static MSABI void *gc_memset(void *d, int c, size_t n) { return memset(d, c, n); }
static MSABI void *gc_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }

/* ---------------- builtin table (name -> Win64-ABI fn) ----------------
 * Registered under the REAL WinCE/coredll API names so payload sources use
 * the exact names the device COREDLL exports. */
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
    B(coredll_puts),
    B(coredll_launch),
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
