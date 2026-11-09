/* Native QA executes the SAME C renderer, menu and icon paths as the SH4
 * build. Only WinCE GDI FillRect is replaced by an RGB565 memory surface. */
#include "phone.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#define _A_SUBDIR 16
struct _finddata_t {
    unsigned attrib;
    unsigned long size;
    char name[260];
};
typedef struct {
    DIR *dir;
    char base[1200];
} HostSearch;
static int _findnext(intptr_t fd, struct _finddata_t *out)
{
    HostSearch *s = (HostSearch *)fd;
    struct dirent *d = readdir(s->dir);
    if (!d)
        return -1;
    char path[1536];
    snprintf(path, sizeof(path), "%s/%s", s->base, d->d_name);
    struct stat st;
    if (stat(path, &st))
        return -1;
    out->attrib = S_ISDIR(st.st_mode) ? 16u : 0u;
    out->size = (unsigned long)st.st_size;
    strncpy(out->name, d->d_name, 259);
    out->name[259] = 0;
    return 0;
}
static intptr_t _findfirst(const char *pattern, struct _finddata_t *out)
{
    HostSearch *s = calloc(1, sizeof(*s));
    if (!s)
        return -1;
    size_t n = strlen(pattern);
    if (n >= sizeof(s->base)) {
        free(s);
        return -1;
    }
    memcpy(s->base, pattern, n + 1);
    if (n && s->base[n - 1] == '*')
        s->base[n - 1] = 0;
    s->dir = opendir(s->base);
    if (!s->dir) {
        free(s);
        return -1;
    }
    if (_findnext((intptr_t)s, out)) {
        closedir(s->dir);
        free(s);
        return -1;
    }
    return (intptr_t)s;
}
static int _findclose(intptr_t fd)
{
    HostSearch *s = (HostSearch *)fd;
    int result = closedir(s->dir);
    free(s);
    return result;
}
#endif
#include "phone_io.h"
static uint16_t pixels[320 * 528];
const Render *render_SpGetThreadContext(void);
const Menu *menu_UIXGetSchemaFactory(void);
const Chrome *chrome_UIXGetSchemaFactory(void);
void menu_StartMenuInitialize(void);
void menu_StartMenuUninitialize(void);
const Keyboard *kb_UIXGetSchemaFactory(void);
const Settings *settings_SettingsGetView(void);
const Catalog *catalog_CatalogGet(void);
int catalog_CatalogReload(void);
int catalog_CatalogPin(unsigned, unsigned);
int svf_SVFValidate(const void *, unsigned);
int svf_SVFDraw(void *, const void *, unsigned, int, int, int, int);
static unsigned file_opens;
static char fsroot[1024];
static U32 sip_flags = 2;
int SipShowIM(U32 flags)
{
    if (flags > 1)
        return 0;
    sip_flags = 2 | flags;
    return 1;
}
int SipGetInfo(SipInfo *s)
{
    if (!s || s->size != sizeof(*s))
        return 0;
    s->flags = sip_flags;
    s->desktop.bottom = (sip_flags & 1) ? 271 : 528;
    return 1;
}
static void path_host(char *out, const W16 *src)
{
    unsigned i = 0, n = 0;
    while (src[i] == '\\')
        ++i;
    assert((src[i] == 'f' || src[i] == 'F') && (src[i + 1] == 'l' || src[i + 1] == 'L'));
    i += 4;
    while (src[i] == '\\')
        ++i;
    while (fsroot[n]) {
        out[n] = fsroot[n];
        ++n;
    }
    out[n++] = '/';
    while (src[i]) {
        assert(n < 1100);
        out[n++] = src[i] == '\\' ? '/' : (char)src[i];
        ++i;
    }
    out[n] = 0;
}
void *CreateFileW(const W16 *p, U32 access, U32 share, void *sec, U32 creation, U32 attrs,
                  void *template_)
{
    ++file_opens;
    (void)share;
    (void)sec;
    (void)attrs;
    (void)template_;
    char path[1200];
    path_host(path, p);
    FILE *f = fopen(path, access & 0x40000000u ? (creation == 2 ? "wb" : "r+b") : "rb");
    return f ? (void *)f : (void *)(intptr_t)-1;
}
int ReadFile(void *f, void *b, U32 size, U32 *done, void *ov)
{
    (void)ov;
    *done = (U32)fread(b, 1, size, f);
    return !ferror(f);
}
int WriteFile(void *f, const void *b, U32 size, U32 *done, void *ov)
{
    (void)ov;
    *done = (U32)fwrite(b, 1, size, f);
    return !ferror(f);
}
int CloseHandle(void *f)
{
    return fclose(f) == 0;
}
U32 GetFileSize(void *f, U32 *high)
{
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, pos, SEEK_SET);
    if (high)
        *high = 0;
    return (U32)size;
}
U32 SetFilePointer(void *f, int offset, int *high, U32 method)
{
    (void)high;
    if (fseek(f, offset, method ? method == 1 ? SEEK_CUR : SEEK_END : SEEK_SET))
        return 0xffffffffu;
    return (U32)ftell(f);
}
static unsigned scan_calls;
int DeleteFileW(const W16 *p)
{
    char path[1200];
    path_host(path, p);
    return remove(path) == 0;
}
static struct {
    int used;
    intptr_t fd;
} finds[8];
static void result(FindData *out, const struct _finddata_t *d)
{
    memset(out, 0, sizeof(*out));
    out->attrs = (d->attrib & _A_SUBDIR) ? 16 : 32;
    out->size_lo = (U32)d->size;
    unsigned i = 0;
    for (; d->name[i] && i < 259; ++i)
        out->name[i] = (unsigned char)d->name[i];
    out->name[i] = 0;
}
void *FindFirstFileW(const W16 *p, FindData *out)
{
    ++scan_calls;
    char path[1200];
    path_host(path, p);
    unsigned i = 0;
    while (i < 8 && finds[i].used)
        ++i;
    assert(i < 8);
    struct _finddata_t d;
    intptr_t fd = _findfirst(path, &d);
    if (fd == -1)
        return (void *)(intptr_t)-1;
    finds[i].fd = fd;
    finds[i].used = 1;
    result(out, &d);
    return (void *)(uintptr_t)(i + 1);
}
int FindNextFileW(void *f, FindData *out)
{
    unsigned i = (unsigned)(uintptr_t)f - 1;
    assert(i < 8 && finds[i].used);
    struct _finddata_t d;
    if (_findnext(finds[i].fd, &d))
        return 0;
    result(out, &d);
    return 1;
}
int FindClose(void *f)
{
    unsigned i = (unsigned)(uintptr_t)f - 1;
    assert(i < 8 && finds[i].used);
    finds[i].used = 0;
    return _findclose(finds[i].fd) == 0;
}
static unsigned fills;
static unsigned long long fill_pixels;
static FILE *draw_profile;
static uint16_t host_rgb565(U32 c)
{
    return (uint16_t)((((c & 255u) >> 3) << 11) | ((((c >> 8) & 255u) >> 2) << 5) |
                      (((c >> 16) & 255u) >> 3));
}
void *CreateSolidBrush(U32 c)
{
    return (void *)(uintptr_t)(0x1000000u | host_rgb565(c));
}
int DeleteObject(void *b)
{
    return b != 0;
}
int FillRect(void *dc, const Rect *rect, void *b)
{
    assert(dc && b);
    assert(rect->left >= 0 && rect->top >= 0 && rect->right <= 320 && rect->bottom <= 528);
    assert(rect->left < rect->right && rect->top < rect->bottom);
    ++fills;
    fill_pixels += (unsigned long long)(rect->right - rect->left) *
                   (unsigned long long)(rect->bottom - rect->top);
    for (int y = rect->top; y < rect->bottom; ++y) {
        for (int x = rect->left; x < rect->right; ++x) {
            pixels[y * 320 + x] = (uint16_t)(uintptr_t)b;
        }
    }
    return 1;
}
/* Minimal test-only window server. Device builds use COREDLL.DLL instead. */
typedef int (*Proc)(void *, U32, U32, U32);
typedef struct {
    U32 style;
    Proc proc;
    int a, b;
    void *i, *icon, *cursor, *brush;
    const W16 *menu, *name;
} HostClass;
static Proc wndproc;
static int alive;
static struct {
    U32 msg, wp, lp;
} events[20];
static unsigned event_count, event_index;
static int script_mode;
static void event(U32 msg, U32 wp, U32 lp)
{
    assert(event_count < 20);
    events[event_count].msg = msg;
    events[event_count].wp = wp;
    events[event_count++].lp = lp;
}
static void script(void)
{
    script_mode = 1;
    event_count = event_index = 0;
}

U32 RegisterClassW(const HostClass *wc)
{
    assert(wc && wc->proc);
    wndproc = wc->proc;
    return 1;
}
void *CreateWindowExW(U32 a, const W16 *b, const W16 *c, U32 d, int x, int y, int w, int h, void *e,
                      void *f, void *g, void *i)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)i;
    assert(x == 0 && y == 0 && w == 320 && h == 528);
    assert(!alive);
    alive = 1;
    return (void *)1;
}
int ShowWindow(void *w, int c)
{
    assert(w && c);
    return 1;
}
int UpdateWindow(void *w)
{
    assert(alive);
    return wndproc(w, WM_PAINT_, 0, 0);
}
int InvalidateRect(void *w, const Rect *a, int b)
{
    (void)a;
    (void)b;
    return w != 0;
}
int GetMessageW(void *m, void *w, U32 a, U32 b)
{
    (void)a;
    (void)b;
    static unsigned step;
    typedef struct {
        void *hwnd;
        U32 message, wp, lp, time;
        Pt pt;
    } HostMsg;
    HostMsg *out = m;
    memset(out, 0, sizeof(*out));
    out->hwnd = w;
    if (script_mode) {
        if (event_index == event_count)
            return 0;
        out->message = events[event_index].msg;
        out->wp = events[event_index].wp;
        out->lp = events[event_index++].lp;
        return 1;
    }
    if (step == 1)
        assert(sip_flags & 1u);
    if (step == 3)
        assert(!(sip_flags & 1u));
    if (step == 4) {
        assert(menu_UIXGetSchemaFactory()->page() == 0);
        return 0;
    }
    out->message = WM_KEY_;
    out->wp = step == 0 || step == 2 ? 0x71 : step == 1 ? 'C' : 0x1b;
    ++step;
    return 1;
}
int TranslateMessage(const void *m)
{
    return m != 0;
}
int DispatchMessageW(const void *m)
{
    const struct {
        void *hwnd;
        U32 message, wp, lp, time;
        Pt pt;
    } *msg = m;
    return wndproc(msg->hwnd, msg->message, msg->wp, msg->lp);
}
int DestroyWindow(void *w)
{
    assert(alive && w);
    alive = 0;
    return 1;
}
void *BeginPaint(void *w, void *p)
{
    (void)p;
    return w;
}
int EndPaint(void *w, const void *p)
{
    (void)p;
    return w != 0;
}
static char launched_path[96];
static unsigned launches, quits;
int coredll_launch(const char *path)
{
    snprintf(launched_path, sizeof(launched_path), "%s", path);
    ++launches;
    return 1;
}
void PostQuitMessage(int code)
{
    (void)code;
    ++quits;
}
int mobile_RunApplication(const AppConfig *);
int phone_main(void);
static void snap(const Menu *m, const Chrome *ch, const char *path)
{
    int height = ch->navheight();
    m->bounds(528 - height);
    const Render *render = render_SpGetThreadContext();
    unsigned before_fills = fills;
    unsigned long long before_pixels = fill_pixels;
    clock_t begin = clock();
    render->surface(528 - height);
    m->paint((void *)1);
    kb_UIXGetSchemaFactory()->dock(height);
    if ((sip_flags & 1u) && !ch->expanded())
        kb_UIXGetSchemaFactory()->paint((void *)1);
    render->surface(528);
    ch->paint((void *)1);
    clock_t end = clock();
    if (draw_profile) {
        const char *name = path;
        for (const char *p = path; *p; ++p)
            if (*p == '/' || *p == '\\')
                name = p + 1;
        fprintf(draw_profile, "%s,%u,%llu,%.3f\n", name, fills - before_fills,
                fill_pixels - before_pixels,
                1000.0 * (double)(end - begin) / (double)CLOCKS_PER_SEC);
        fflush(draw_profile);
    }
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n320 528\n255\n");
    for (int i = 0; i < 320 * 528; ++i) {
        unsigned p = pixels[i];
        unsigned char rgb[3] = {(unsigned char)((p >> 11) * 255 / 31),
                                (unsigned char)(((p >> 5) & 63) * 255 / 63),
                                (unsigned char)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static U32 kbpos(int x, int y)
{
    return (U32)x | ((U32)(y - chrome_UIXGetSchemaFactory()->navheight()) << 16);
}
static U32 pos(int x, int y)
{
    return (U32)x | ((U32)y << 16);
}
static void click(const Menu *m, int x, int y)
{
    m->input(WM_DOWN_, 0, pos(x, y));
    m->input(WM_UP_, 0, pos(x, y));
}
static void named_snap(const Menu *m, const Chrome *c, const char *folder, const char *name)
{
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s.ppm", folder, name);
    snap(m, c, path);
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    char profile_path[1200];
    snprintf(profile_path, sizeof(profile_path), "%s/draw-profile.csv", argv[1]);
    draw_profile = fopen(profile_path, "w");
    assert(draw_profile);
    fprintf(draw_profile, "scene,fill_calls,painted_pixels,host_cpu_ms\n");
    strncpy(fsroot, argv[2], sizeof(fsroot) - 1);
    const Menu *m = menu_UIXGetSchemaFactory();
    const Chrome *ch = chrome_UIXGetSchemaFactory();
    const Keyboard *kb = kb_UIXGetSchemaFactory();
    menu_StartMenuInitialize();
    const Catalog *cat = catalog_CatalogGet();
    assert(cat->count == 27);
    unsigned scans = scan_calls, opens = file_opens;
    assert(scans > 0);
    assert(catalog_CatalogReload() == 27 && scan_calls == scans && file_opens - opens == 2);
    char cache_path[1200];
    snprintf(cache_path, sizeof(cache_path), "%s/apps.dat", fsroot);
    FILE *broken = fopen(cache_path, "wb");
    assert(broken);
    fwrite("CAT", 1, 3, broken);
    fclose(broken);
    assert(catalog_CatalogReload() == 27 && scan_calls > scans);
    scans = scan_calls;
    assert(catalog_CatalogReload() == 27 && scan_calls == scans);
    unsigned default_pins = cat->pin_count;
    assert(default_pins >= 20);
    int camera = -1, zeta = -1, plain = -1, raster = -1;
    for (unsigned i = 0; i < cat->count; ++i) {
        const AppEntry *a = &cat->apps[i];
        if (!strcmp(a->name, "Camera"))
            camera = (int)i;
        if (!strcmp(a->name, "Zeta"))
            zeta = (int)i;
        if (!strcmp(a->name, "Plain"))
            plain = (int)i;
        if (!strcmp(a->name, "Raster"))
            raster = (int)i;
    }
    assert(camera >= 0 && zeta >= 0 && plain >= 0 && raster >= 0);
    assert(cat->apps[camera].icon_len && cat->apps[zeta].icon_len);
    assert(!cat->apps[plain].icon_len && !cat->apps[plain].pe_icon_type);
    assert(cat->apps[raster].pe_icon_type);
    const AppEntry *a = &cat->apps[camera];
    for (unsigned n = 0; n < a->icon_len; ++n)
        assert(!svf_SVFValidate(a->icon, n));
    assert(svf_SVFValidate(a->icon, a->icon_len));
    unsigned before = fills;
    assert(!svf_SVFDraw((void *)1, "bad", 3, 0, 0, 32, 32));
    assert(before == fills);
    unsigned baseline = launches;
    m->input(WM_DOWN_, 0, pos(24, 44));
    assert(launches == baseline);
    m->input(WM_DOWN_, 0, pos(24, 44));
    assert(launches == baseline);
    m->input(WM_UP_, 0, pos(24, 44));
    assert(launches == baseline + 1 && strstr(launched_path, "phone.exe"));
    m->input(WM_DOWN_, 0, pos(24, 44));
    m->input(WM_MOVE_, 0, pos(24, 120));
    m->input(WM_UP_, 0, pos(24, 120));
    assert(launches == baseline + 1);
    m->input(WM_KEY_, 0x28, 0);
    m->input(WM_KEY_, 13, 0);
    assert(launches == baseline + 2 && strstr(launched_path, "phone.exe"));
    m->input(WM_DOWN_, 0, pos(34, 360));
    m->input(WM_UP_, 0, pos(34, 360));
    assert(strstr(launched_path, "calc.exe"));
    m->input(WM_DOWN_, 0, pos(156, 400));
    m->input(WM_UP_, 0, pos(156, 400));
    assert(strstr(launched_path, "settings.exe"));
    // Firmware can report movement as repeated DOWN and omit intermediate MOVE.
    baseline = launches;
    m->input(WM_DOWN_, 0, pos(24, 44));
    m->input(WM_DOWN_, 0, pos(24, 130));
    m->input(WM_DOWN_, 0, pos(24, 130));
    m->input(WM_UP_, 0, pos(24, 130));
    assert(launches == baseline);
    m->input(WM_DOWN_, 0, pos(24, 44));
    m->input(WM_UP_, 0, pos(24, 140));
    assert(launches == baseline);
    menu_StartMenuInitialize();
    named_snap(m, ch, argv[1], "home");
    m->input(WM_KEY_, 0x28, 0);
    m->input(WM_KEY_, 0x28, 0);
    named_snap(m, ch, argv[1], "home-scrolled");
    m->input(WM_KEY_, 0x27, 0);
    named_snap(m, ch, argv[1], "all-apps");
    m->input(WM_DOWN_, 0, pos(80, 350));
    m->input(WM_MOVE_, 0, pos(80, 160));
    assert(m->scroll() > 0);
    for (unsigned i = 0; i < 4; ++i)
        m->input(WM_KEY_, 0x26, 0);
    click(m, 20, 44);
    assert(sip_flags & 1u);
    named_snap(m, ch, argv[1], "keyboard-lower");
    assert(kb->input(WM_DOWN_, 0, kbpos(15, 340)) == 'q');
    kb->input(WM_DOWN_, 0, kbpos(20, 445));
    assert(kb->page() == 3);
    named_snap(m, ch, argv[1], "keyboard-caps");
    assert(kb->input(WM_DOWN_, 0, kbpos(15, 340)) == 'Q');
    kb->input(WM_DOWN_, 0, kbpos(20, 500));
    assert(kb->page() == 1);
    named_snap(m, ch, argv[1], "keyboard-numbers");
    assert(kb->input(WM_DOWN_, 0, kbpos(15, 340)) == '1');
    kb->input(WM_DOWN_, 0, kbpos(20, 445));
    assert(kb->page() == 2);
    named_snap(m, ch, argv[1], "keyboard-symbols");
    assert(kb->input(WM_DOWN_, 0, kbpos(15, 392)) == '^');
    assert(kb->input(WM_DOWN_, 0, kbpos(294, 445)) == 8);
    const char *q = "Camera";
    for (; *q; ++q)
        m->input(WM_CHAR_, (unsigned char)*q, 0);
    named_snap(m, ch, argv[1], "search");
    m->input(WM_KEY_, 0x25, 0);
    assert(!(sip_flags & 1));
    m->input(WM_KEY_, 0x25, 0);
    m->input(WM_KEY_, 0x27, 0);
    click(m, 10, 90);
    named_snap(m, ch, argv[1], "letters");
    assert(pixels[100 * 320] == host_rgb565(SURFACE));
    click(m, 240, 256);
    ch->input(WM_DOWN_, 0, pos(30, 10));
    ch->input(WM_MOVE_, 0, pos(30, 200));
    assert(ch->expanded());
    ch->input(WM_UP_, 0, pos(30, 200));
    named_snap(m, ch, argv[1], "notifications");
    ch->input(WM_DOWN_, 0, pos(30, 90));
    ch->input(WM_UP_, 0, pos(30, 90));
    named_snap(m, ch, argv[1], "notification-toggle");
    ch->input(WM_KEY_, 0x25, 0);
    assert(!ch->expanded());
    ch->input(WM_DOWN_, 0, pos(30, 10));
    ch->input(WM_MOVE_, 0, pos(30, 200));
    ch->input(WM_UP_, 0, pos(30, 200));
    ch->input(WM_DOWN_, 0, pos(150, 480));
    ch->input(WM_MOVE_, 0, pos(150, 430));
    assert(!ch->expanded());
    ch->input(WM_UP_, 0, pos(150, 430));
    ch->input(WM_DOWN_, 0, pos(30, 10));
    ch->input(WM_MOVE_, 0, pos(30, 200));
    ch->input(WM_UP_, 0, pos(30, 200));
    ch->input(WM_DOWN_, 0, pos(160, 520));
    assert(ch->input(WM_UP_, 0, pos(160, 450)) == 1 && !ch->expanded());
    ch->input(WM_DOWN_, 0, pos(30, 10));
    ch->input(WM_MOVE_, 0, pos(30, 200));
    ch->input(WM_UP_, 0, pos(30, 200));
    ch->input(WM_DOWN_, 0, pos(200, 220));
    assert(ch->input(WM_UP_, 0, pos(200, 220)) == 3 && !ch->expanded());
    unsigned id = cat->apps[plain].id;
    assert(catalog_CatalogPin(id, 1));
    assert(catalog_CatalogReload() == 27);
    assert(cat->pin_count == default_pins + 1);
    assert(catalog_CatalogPin(id, 2));
    assert(catalog_CatalogReload() == 27);
    assert(cat->pin_count == default_pins + 1);
    assert(catalog_CatalogPin(id, 0));
    assert(catalog_CatalogReload() == 27 && cat->pin_count == default_pins);
    assert(ch->navheight() == 40);
    ch->input(WM_DOWN_, 0, pos(60, 508));
    assert(ch->input(WM_UP_, 0, pos(60, 508)) == 4);
    ch->input(WM_DOWN_, 0, pos(160, 508));
    assert(ch->input(WM_UP_, 0, pos(160, 508)) == 5);
    ch->input(WM_DOWN_, 0, pos(260, 508));
    assert(ch->input(WM_UP_, 0, pos(260, 508)) == 6);
    ch->navigation(0);
    assert(ch->navheight() == 0);
    named_snap(m, ch, argv[1], "home-no-softkeys");
    ch->navigation(1);
    m->bounds(488);
    m->open(6);
    named_snap(m, ch, argv[1], "excel-splash");
    m->input(WM_KEY_, 13, 0);
    named_snap(m, ch, argv[1], "excel-employee");
    click(m, 25, 60);
    named_snap(m, ch, argv[1], "excel-project");
    m->open(14);
    named_snap(m, ch, argv[1], "news-app");
    m->open(11);
    named_snap(m, ch, argv[1], "edge-app");
    m->open(APP_SETTINGS_ID);
    const Settings *settings = settings_SettingsGetView();
    assert(settings->page() == 0);
    named_snap(m, ch, argv[1], "settings-main");
    click(m, 100, 148);
    assert(settings->page() == 1);
    named_snap(m, ch, argv[1], "settings-system");
    click(m, 90, 200);
    assert(settings->page() == 2);
    named_snap(m, ch, argv[1], "settings-display");
    click(m, 200, 182);
    click(m, 30, 268);
    click(m, 50, 455);
    click(m, 180, 455);
    m->input(WM_KEY_, 0x1b, 0);
    assert(settings->page() == 1);
    m->input(WM_KEY_, 0x1b, 0);
    assert(settings->page() == 0);
    m->input(WM_KEY_, 0x1b, 0);
    assert(m->page() == 0);
    m->open(APP_SETTINGS_ID);
    click(m, 100, 148);
    click(m, 90, 200);
    assert(settings->page() == 2);
    click(m, 24, 48);
    assert(settings->page() == 0);
    m->input(WM_DOWN_, 0, pos(150, 470));
    m->input(WM_MOVE_, 0, pos(150, 200));
    m->input(WM_UP_, 0, pos(150, 200));
    assert(settings->page() == 0);
    named_snap(m, ch, argv[1], "settings-scrolled");
    click(m, 100, 480);
    assert(settings->page() == 4);
    named_snap(m, ch, argv[1], "settings-update-security");
    click(m, 100, 200);
    assert(settings->page() == 7);
    named_snap(m, ch, argv[1], "settings-phone-update");
    m->input(WM_KEY_, 0x1b, 0);
    click(m, 100, 290);
    assert(settings->page() == 6);
    named_snap(m, ch, argv[1], "settings-encryption");
    m->input(WM_KEY_, 0x1b, 0);
    click(m, 100, 390);
    assert(settings->page() == 5);
    named_snap(m, ch, argv[1], "settings-developers");
    click(m, 24, 48);
    assert(settings->page() == 0);
    m->open(7);
    named_snap(m, ch, argv[1], "files-root");
    click(m, 100, 140);
    named_snap(m, ch, argv[1], "files-apps");
    unsigned exited = quits;
    m->input(WM_KEY_, 0x1b, 0);
    assert(quits == exited);
    m->input(WM_KEY_, 0x1b, 0);
    assert(quits == exited + 1);
    menu_StartMenuUninitialize();
    assert(phone_main() == 0);
    assert(!alive);
    assert(!(sip_flags & 1u));
    for (unsigned id = 1; id <= 24; ++id) {
        if (id == 2)
            continue;
        menu_StartMenuInitialize();
        AppConfig config = {0};
        config.reserved[0] = id;
        assert(mobile_RunApplication(&config) == 0);
        unsigned exits = quits;
        m->input(WM_KEY_, 0x1b, 0);
        assert(quits == exits + 1);
        menu_StartMenuUninitialize();
    }
    script();
    menu_StartMenuInitialize();
    AppConfig cfg = {0};
    cfg.reserved[0] = 7;
    unsigned exits = quits;
    event(WM_DOWN_, 0, pos(100, 140));
    event(WM_UP_, 0, pos(100, 140));
    event(WM_DOWN_, 0, pos(60, 508));
    event(WM_UP_, 0, pos(60, 508));
    event(WM_DOWN_, 0, pos(160, 508));
    event(WM_UP_, 0, pos(160, 508));
    assert(mobile_RunApplication(&cfg) == 0);
    assert(quits == exits + 1 && !strcmp(launched_path, "\\fls0\\STARTHOST.EXE"));
    menu_StartMenuUninitialize();
    script();
    menu_StartMenuInitialize();
    cfg.reserved[0] = 6;
    exits = quits;
    event(WM_DOWN_, 0, pos(260, 508));
    event(WM_UP_, 0, pos(260, 508));
    event(WM_DOWN_, 0, pos(60, 508));
    event(WM_UP_, 0, pos(60, 508));
    assert(mobile_RunApplication(&cfg) == 0);
    assert(!(sip_flags & 1u) && m->page() == 1 && quits == exits);
    menu_StartMenuUninitialize();
    script();
    menu_StartMenuInitialize();
    cfg.reserved[0] = 0;
    event(WM_DOWN_, 0, pos(30, 10));
    event(WM_MOVE_, 0, pos(30, 200));
    event(WM_UP_, 0, pos(30, 200));
    event(WM_DOWN_, 0, pos(160, 520));
    event(WM_UP_, 0, pos(160, 450));
    assert(mobile_RunApplication(&cfg) == 0);
    assert(!ch->expanded() && pixels[400 * 320 + 20] != 0);
    menu_StartMenuUninitialize();
    script_mode = 0;
    printf("PASS engine: Back navigates Files, Home queues StartHost, Search opens apps, Back "
           "hides SIP, notification handle release repaints\n");
    printf("PASS cached startup uses exactly two file opens and zero scans; truncated cache "
           "rebuilds; all 23 app configurations open and close\n");
    for (unsigned i = 0; i < 8; ++i)
        assert(!finds[i].used);
    printf(
        "PASS 24 discovered apps + 3 fallback fixtures, embedded SVF/DIB/letter fallbacks, pin "
        "persistence, keyboard pages, SIP, gestures, notifications, native lifecycle (%u fills)\n",
        fills);
    return 0;
}
