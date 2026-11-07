/* coredll.c -- COREDLL for NTOS, big-endian SH4 (machine 0x01A8).
 *
 * Modeled on WinCE's coredll.dll: the single module every EXE imports its
 * base services from. NTOS copy-loads it into the heap, UTLB-maps its
 * 4 KiB pages at the preferred base 0x00800000 and resolves payload
 * imports against the PE export directory elf2ntx.py builds.
 *
 * Exports (tools/coredll_exports.txt, parsed from WinCE 4.2
 * coredll.def, ~1900 names): a handful are really implemented below,
 * everything else shares one `coredll_stub` (returns 0) so programs can
 * be compiled against the full WinCE API surface and get implementations
 * filled in one by one.
 *
 * The real ones reach NTOS services the way an OS330 C1A add-in reaches
 * the Casio OS: through the API window at VA 0x08100000, whose first
 * word points at the syscall table (NTOS rebuilds that window in UTLB
 * slots 0x37..0x3A before the payload runs).
 */
typedef unsigned int u32;

/* OS330 C1A API window contract: *(0x08100000) = syscall table pointer. */
#define API_WINDOW ((const void *const *)0x08100000u)

/* NtosApi field order == table order:
 *   [0] puts(s) [1] printf [2] refresh [3] malloc(n) [4] free(p)
 *   [5] get_key() [6] clear [7] pixel [8] line [9] fillrect [10] input
 */
static const void **ntapi(void)
{
    const void *tbl = *API_WINDOW;
    return (const void **)(tbl != 0 ? tbl : 0);
}

void coredll_puts(const char *s)
{
    const void **api = ntapi();
    if (api != 0 && api[0] != 0)
        ((void (*)(const char *))api[0])(s);
}

u32 coredll_getkey(void)
{
    const void **api = ntapi();
    if (api != 0 && api[5] != 0)
        return ((u32 (*)(void))api[5])();
    return 0;
}

void coredll_refresh(void)
{
    const void **api = ntapi();
    if (api != 0 && api[2] != 0)
        ((void (*)(void))api[2])();
}

void *coredll_malloc(u32 n)
{
    const void **api = ntapi();
    if (api != 0 && api[3] != 0)
        return ((void *(*)(u32))api[3])(n);
    return 0;
}

void coredll_free(void *p)
{
    const void **api = ntapi();
    if (api != 0 && api[4] != 0)
        ((void (*)(void *))api[4])(p);
}

/* ---------------------------------------------------------------------------
 * Really-implemented WinCE APIs
 * ------------------------------------------------------------------------- */

/* WinCE MEMORYSTATUS (GlobalMemoryStatus) */
typedef struct {
    u32 dwLength;
    u32 dwMemLoad;
    u32 dwTotalPhys;
    u32 dwAvailPhys;
    u32 dwTotalPageFile;
    u32 dwAvailPageFile;
    u32 dwTotalVirtual;
    u32 dwAvailVirtual;
} MEMSTATUS;

/* WinCE NKDbgPrintfW is a wide-string API. Convert to a bounded, NUL-
 * terminated byte buffer before crossing into the NTOS console service.
 * Never interpret a UTF-16 pointer as char* (the old ABI was wrong). */
void NKDbgPrintfW(const unsigned short *wide)
{
    char out[80];
    unsigned i = 0;
    if (wide == 0) return;
    while (i + 1 < sizeof(out) && wide[i] != 0) {
        unsigned short ch = wide[i];
        out[i++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
    }
    out[i] = 0;
    coredll_puts(out);
}

/* Kernel-mode toggles: NTOS payloads run privileged already, so both are
 * acknowledgements that preserve the original return contracts. */
u32 SetKMode(u32 enable)
{
    (void)enable;
    return 1; /* previous mode: kernel */
}

u32 SetProcPermissions(u32 newperms)
{
    (void)newperms;
    return 0xFFFFFFFFu; /* previous permissions: everything */
}

void *LocalAlloc(u32 flags, u32 bytes)
{
    (void)flags; /* LMEM_FIXED semantics only */
    return coredll_malloc(bytes);
}

u32 LocalFree(void *p)
{
    coredll_free(p);
    return 0; /* NO_ERROR */
}

void *VirtualAlloc(u32 base, u32 size, u32 type, u32 protection)
{
    (void)base;
    (void)type;
    (void)protection; /* MEM_COMMIT + PAGE_READWRITE semantics only */
    return coredll_malloc(size);
}

u32 VirtualFree(void *base, u32 size, u32 freetype)
{
    (void)size;
    (void)freetype;
    coredll_free((void *)base);
    return 1;
}

/* GlobalMemoryStatus with *real* numbers: total = the known HH3 heap
 * size, avail = the largest contiguous block we can actually obtain by
 * descending probe (which doubles as the kernel-memory exercise). */
void GlobalMemoryStatus(MEMSTATUS *ms)
{
    const u32 total = 12u * 1024u * 1024u;
    u32 avail = 0;
    u32 lo = 0x1000, hi = total;
    if (ms == 0)
        return;
    /* binary-probe the largest successful malloc */
    while (lo <= hi) {
        const u32 mid = (lo + hi) / 2u;
        void *p = coredll_malloc(mid);
        if (p != 0) {
            coredll_free(p);
            avail = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    ms->dwLength = sizeof(MEMSTATUS);
    ms->dwMemLoad = 100 - (u32)((unsigned long long)avail * 100 / total);
    ms->dwTotalPhys = total;
    ms->dwAvailPhys = avail;
    ms->dwTotalPageFile = total;
    ms->dwAvailPageFile = avail;
    ms->dwTotalVirtual = total;
    ms->dwAvailVirtual = avail;
}

/* COREDLL's GDI compatibility layer. NTOS is the kernel behind this API:
 * handles are synthetic, colors are Win32 COLORREF (0x00BBGGRR), and
 * the NTOS gates accept 16-bit RGB565. This is not a full window manager. */
typedef unsigned short u16;
typedef struct { int left, top, right, bottom; } NT_RECT;
typedef struct {
    void *hdc;
    int erase;
    NT_RECT rcPaint;
    int restore, incUpdate;
    unsigned char reserved[32];
} NT_PAINT;
typedef struct { int x, y; } NT_POINT;

static u32 rgb565(u32 colorref)
{
    u32 r = colorref & 255u;
    u32 g = (colorref >> 8) & 255u;
    u32 b = (colorref >> 16) & 255u;
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void ntos_clear(u32 colorref)
{
    const void **a = ntapi();
    if (a && a[6]) ((void (*)(u32))a[6])(rgb565(colorref));
}

static void ntos_pixel(u32 x, u32 y, u32 colorref)
{
    const void **a = ntapi();
    if (a && a[7]) ((void (*)(u32,u32,u32))a[7])(x,y,rgb565(colorref));
}

static void ntos_line(u32 x1, u32 y1, u32 x2, u32 y2, u32 colorref)
{
    const void **a = ntapi();
    if (a && a[8]) ((void (*)(u32,u32,u32,u32,u32))a[8])
        (x1,y1,x2,y2,rgb565(colorref));
}

static void ntos_fillrect(u32 l, u32 t, u32 r, u32 b, u32 colorref)
{
    const void **a = ntapi();
    if (a && a[9]) ((void (*)(u32,u32,u32,u32,u32))a[9])
        (l,t,r,b,rgb565(colorref));
}

/* Blocking. Returns 1 on touch and 2 on CLEAR, with touch coordinates. */
static u32 ntos_touch(u32 *x, u32 *y)
{
    const void **a = ntapi();
    if (a && a[10]) return ((u32 (*)(u32 *,u32 *))a[10])(x,y);
    return 2;
}

/* WinCE MSG layout (32-bit SH4 ABI). Touch arrives as WM_LBUTTONDOWN;
 * CLEAR produces WM_QUIT. This is a minimal message-pump adapter, not
 * a complete WinCE desktop or window manager. */
typedef struct {
    void *hwnd;
    u32 message;
    u32 wParam;
    u32 lParam;
    u32 time;
    NT_POINT pt;
} NT_MSG;
int GetMessageW(NT_MSG *msg, void *hwnd, u32 min_filter, u32 max_filter)
{
    u32 x=0, y=0, direction=0, ev;
    const void **a=ntapi();
    (void)min_filter; (void)max_filter;
    if (!msg || !a || !a[15]) return -1;
    do {
        ev=((u32 (*)(u32 *,u32 *,u32 *))a[15])(&x,&y,&direction);
    } while (ev == 0);
    msg->hwnd=hwnd;
    msg->wParam=0;
    msg->time=0;
    msg->pt.x=0;
    msg->pt.y=0;
    msg->lParam=0;
    if (ev == 2) {
        msg->message=0x0012u; /* WM_QUIT */
        return 0;
    }
    if (ev == 3) {
        /* Hollyhock keycodes match the WinCE VK arrows, Enter and
         * ASCII digit values used by our demos. */
        msg->message=0x0100u; /* WM_KEYDOWN */
        /* Normalize Hollyhock keypad codes into standard WinCE VK_*. */
        if (x == 0x0090u) x=0x26u;      /* VK_UP */
        else if (x == 0x0091u) x=0x28u; /* VK_DOWN */
        else if (x == 0x0092u) x=0x25u; /* VK_LEFT */
        else if (x == 0x0093u) x=0x27u; /* VK_RIGHT */
        else if (x == 0x0094u) x=0x0Du; /* VK_RETURN */
        msg->wParam=x;
        return 1;
    }
    msg->message=0x0201u; /* stylus / WM_LBUTTONDOWN */
    msg->pt.x=(int)x;
    msg->pt.y=(int)y;
    msg->lParam=((y & 0xFFFFu)<<16) | (x & 0xFFFFu);
    return 1;
}

void *GetDC(void *hwnd)
{
    (void)hwnd;
    return (void *)1;
}

int ReleaseDC(void *hwnd, void *hdc)
{
    (void)hwnd;
    if (hdc) coredll_refresh();
    return hdc ? 1 : 0;
}

void *BeginPaint(void *hwnd, NT_PAINT *paint)
{
    (void)hwnd;
    if (paint) {
        paint->hdc = (void *)1;
        paint->erase = 0;
        paint->restore = 0;
        paint->incUpdate = 0;
    }
    return (void *)1;
}

int EndPaint(void *hwnd, const NT_PAINT *paint)
{
    (void)hwnd; (void)paint;
    coredll_refresh();
    return 1;
}

u32 SetPixel(void *hdc, int x, int y, u32 colorref)
{
    if (hdc && x >= 0 && y >= 0)
        ntos_pixel((u32)x, (u32)y, colorref);
    return colorref;
}

void *CreateSolidBrush(u32 colorref)
{
    /* Color is encoded in the synthetic brush handle, bit 24 marks validity. */
    return (void *)(0x01000000u | rgb565(colorref));
}

int DeleteObject(void *object)
{
    return object != 0;
}

int FillRect(void *hdc, const NT_RECT *rect, void *brush)
{
    if (!hdc || !rect || !brush) return 0;
    u32 c = (u32)brush & 0xFFFFu;
    const void **a = ntapi();
    if (a && a[9] && rect->left >= 0 && rect->top >= 0 &&
        rect->right > rect->left && rect->bottom > rect->top) {
        ((void (*)(u32,u32,u32,u32,u32))a[9])
            ((u32)rect->left, (u32)rect->top,
             (u32)rect->right, (u32)rect->bottom, c);
        return 1;
    }
    return 0;
}

static int pen_x, pen_y;
int MoveToEx(void *hdc, int x, int y, NT_POINT *previous)
{
    if (!hdc) return 0;
    if (previous) { previous->x = pen_x; previous->y = pen_y; }
    pen_x = x; pen_y = y;
    return 1;
}

int LineTo(void *hdc, int x, int y)
{
    if (!hdc) return 0;
    if (pen_x >= 0 && pen_y >= 0 && x >= 0 && y >= 0)
        ntos_line((u32)pen_x, (u32)pen_y, (u32)x, (u32)y, 0x00FFFFFFu);
    pen_x = x; pen_y = y;
    return 1;
}

int Rectangle(void *hdc, int l, int t, int r, int b)
{
    if (!hdc || l < 0 || t < 0 || r <= l || b <= t) return 0;
    ntos_line((u32)l,(u32)t,(u32)(r-1),(u32)t,0xFFFFFFu);
    ntos_line((u32)(r-1),(u32)t,(u32)(r-1),(u32)(b-1),0xFFFFFFu);
    ntos_line((u32)(r-1),(u32)(b-1),(u32)l,(u32)(b-1),0xFFFFFFu);
    ntos_line((u32)l,(u32)(b-1),(u32)l,(u32)t,0xFFFFFFu);
    return 1;
}

void OutputDebugStringW(const u16 *wide)
{
    char buf[64];
    unsigned i = 0;
    if (!wide) return;
    while (i < sizeof(buf) - 1 && wide[i]) {
        buf[i] = (wide[i] < 128) ? (char)wide[i] : '?';
        ++i;
    }
    buf[i] = 0;
    coredll_puts(buf);
}

/* Working WinCE-compatible display helpers (single full-screen DC). */
int GetClientRect(void *hwnd, NT_RECT *rect)
{
    (void)hwnd;
    if (!rect) return 0;
    rect->left=0; rect->top=0; rect->right=320; rect->bottom=528;
    return 1;
}
int GetSystemMetrics(int index)
{
    if (index==0) return 320; /* SM_CXSCREEN */
    if (index==1) return 528; /* SM_CYSCREEN */
    return 0;
}
int TextOutW(void *hdc, int x, int y, const u16 *text, int n)
{
    const void **a=ntapi();
    char ansi[64];
    int i=0;
    if (!hdc || !text || !a || !a[16] || x<0 || y<0 || n<0) return 0;
    while (i<n && i<63 && text[i]) {
        ansi[i]=(text[i]>=32 && text[i]<127) ? (char)text[i] : '?';
        ++i;
    }
    ansi[i]=0;
    ((void (*)(u32,u32,const char *))a[16])((u32)x,(u32)y,ansi);
    return 1;
}
int InvalidateRect(void *hwnd, const NT_RECT *rect, int erase)
{
    (void)hwnd; (void)rect; (void)erase;
    /* No async window manager yet; next BeginPaint always repaints. */
    return 1;
}
int UpdateWindow(void *hwnd)
{
    (void)hwnd;
    coredll_refresh();
    return 1;
}

/* WinCE file-handle bridge. Handles are tagged SDK file descriptors.
 * Strings from apps are UTF-16; Hollyhock File_Open takes an ANSI path.
 * Only the existing WinCE function names are exposed to PE clients. */
#define NT_INVALID_FILE ((void *)-1)
#define NT_HANDLE_TAG 0x00010000u
#define NT_GENERIC_READ  0x80000000u
#define NT_GENERIC_WRITE 0x40000000u
#define NT_CREATE_ALWAYS 2u
#define NT_OPEN_EXISTING 3u
#define NT_OPEN_ALWAYS 4u
#define NT_FILE_READ 1u
#define NT_FILE_WRITE 2u
#define NT_FILE_CREATE 4u

static int narrow_path(const u16 *src, char *dest, u32 capacity)
{
    u32 i=0, j=0;
    if (!src || capacity < 12u) return 0;
    /* The Hollyhock SDK uses "\\fls0\file" (two leading backslashes).
     * Accept either the WinCE-style single leading slash or the canonical
     * SDK spelling, then always pass the canonical form to File_Open. */
    while (src[i] == '\\' && i < 2u) ++i;
    if (i == 0 || !((src[i]=='f'||src[i]=='F') &&
                    (src[i+1]=='l'||src[i+1]=='L') &&
                    (src[i+2]=='s'||src[i+2]=='S') &&
                    src[i+3]=='0' && src[i+4]=='\\'))
        return 0;
    dest[j++]='\\';
    dest[j++]='\\';
    while (src[i] && j+1 < capacity) {
        if (src[i] > 127u) return 0;
        dest[j++]=(char)src[i++];
    }
    if (src[i] != 0) return 0;
    dest[j]=0;
    return 1;
}

static int handle_fd(void *handle)
{
    u32 value=(u32)handle;
    if ((value & 0xFFFF0000u) != NT_HANDLE_TAG ||
        (value & 0xFFFFu) == 0u) return -1;
    return (int)(value & 0xFFFFu)-1;
}

void *CreateFileW(const u16 *path, u32 access, u32 share,
                  void *security, u32 creation, u32 attributes,
                  void *template_file)
{
    const void **a=ntapi();
    char narrow[96];
    u32 flags=0;
    int fd;
    (void)share; (void)security; (void)attributes; (void)template_file;
    if (!a || !a[11] || !narrow_path(path,narrow,sizeof(narrow)))
        return NT_INVALID_FILE;
    if (access & NT_GENERIC_READ) flags |= NT_FILE_READ;
    if (access & NT_GENERIC_WRITE) flags |= NT_FILE_WRITE;
    if (creation == NT_CREATE_ALWAYS || creation == NT_OPEN_ALWAYS)
        flags |= NT_FILE_CREATE;
    else if (creation != NT_OPEN_EXISTING) return NT_INVALID_FILE;
    if (!(flags & (NT_FILE_READ|NT_FILE_WRITE))) return NT_INVALID_FILE;
    fd=((int (*)(const char *,u32))a[11])(narrow,flags);
    if (fd < 0 || fd >= 0xFFFF) return NT_INVALID_FILE;
    return (void *)(NT_HANDLE_TAG | ((u32)fd+1u));
}

u32 GetFileSize(void *handle, u32 *high)
{
    const void **a=ntapi();
    int fd=handle_fd(handle);
    int size;
    if (high) *high=0;
    if (fd<0 || !a || !a[17]) return 0xFFFFFFFFu;
    size=((int (*)(int))a[17])(fd);
    return size<0 ? 0xFFFFFFFFu : (u32)size;
}

int ReadFile(void *handle, void *buffer, u32 count, u32 *done, void *overlapped)
{
    const void **a=ntapi();
    int fd=handle_fd(handle), n;
    if (done) *done=0;
    if (fd < 0 || !buffer || !a || !a[12] || overlapped || count > 4096u)
        return 0;
    n=((int (*)(int,void *,u32))a[12])(fd,buffer,count);
    if (n < 0) return 0;
    if (done) *done=(u32)n;
    return 1;
}

int WriteFile(void *handle, const void *buffer, u32 count, u32 *done,
              void *overlapped)
{
    const void **a=ntapi();
    int fd=handle_fd(handle), n;
    if (done) *done=0;
    if (fd < 0 || !buffer || !a || !a[13] || overlapped || count > 4096u)
        return 0;
    n=((int (*)(int,const void *,u32))a[13])(fd,buffer,count);
    if (n < 0) return 0;
    if (done) *done=(u32)n;
    return 1;
}

int CloseHandle(void *handle)
{
    const void **a=ntapi();
    int fd=handle_fd(handle);
    if (fd < 0 || !a || !a[14]) return 0;
    return ((int (*)(int))a[14])(fd)==0;
}

/* ---------------------------------------------------------------------------
 * Stub fallback: every remaining export in tools/coredll_exports.txt is
 * redirected here by the packer (--exports-file + --stub-symbol).
 * ------------------------------------------------------------------------- */
int coredll_stub(void)
{
    return 0;
}

/* Entry point -- real DLLs have no meaningful one; NTOS never calls it. */
int main(void)
{
    return 0;
}
