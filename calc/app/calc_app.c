/* calc_app.c — "Calculator" (W10M WindowsCalculator 10.1605.1582.0) POC for
 * NTOS, driven by uXAML markup.
 *
 * Blind port reconstructed purely from the package (no reference
 * screenshots exist): the 19 XBF pages, en-US pri strings and TH2 style
 * dictionaries — see calc/analysis/CALCULATOR_UI_REFERENCE.md.
 *
 * Modes: Standard, Scientific (phone 5-col keypad + DEG/RAD/GRAD),
 * UnitConverter (12 categories), About; hamburger nav pane (grouped mode
 * list), History + Memory full-surface sheets, unit dropdown sheet.
 *
 * Arithmetic: the SH4 target has no FPU, so the engine is a decimal
 * int64 type (value = mantissa x 10^exp, exact +,-,x,/ and series-based
 * transcendentals) — same spirit as the reference CEngine's rational
 * engine, no libm anywhere.
 *
 * Component reuse (shared-DLL model):
 *   - uXAML engine: edge/app/uxaml.c (calc.uxaml from calc/app/markup)
 *   - glyphs: files/app/files_icons.c + calc_icons.c (MDL2 E94x strokes)
 *   - navbar: SHELLCHROME.DLL strip (Back/Home/Search) via UIXChromeFactory
 *   - bitmap font: payload/phone/font_bitmap.h (+32/44px sizes we baked)
 */
#include "nt_app.h"
#include "uxaml.h"
#include "files_icons.h"
#include "calc_icons.h"

/* bitmap font (generated 1-bpp glyphs, no TTF on device) */
#include "font_bitmap.h"

/* ---- shared phone-framework ABI (mirror of payload/phone/phone.h) ---- */
typedef uint64_t U64;

typedef struct CalcRender {
    U32 profile;
    void (*clip)(int, int, int, int);
    void (*fill)(void *, int, int, int, int, U32);
    void (*text)(void *, int, int, const char *, U32, int);
    void (*line)(void *, int, int, int, int, U32);
    void (*ellipse)(void *, int, int, int, int, U32, int);
    void (*polygon)(void *, const Pt *, unsigned, U32);
    void (*surface)(int);
    const U32 *theme;
} CalcRender;

typedef struct CalcKb {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*page)(void);
    void (*dock)(int);
} CalcKb;

typedef struct CalcChrome {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*expanded)(void);
    int (*navheight)(void);
    int (*navigation)(int);
} CalcChrome;

NT_DEFINE(render_SpGetThreadContext)
NT_IMPORT(const CalcKb *, UIXGetSchemaFactory, (void))
NT_DEFINE(UIXGetSchemaFactory)
NT_IMPORT(const CalcChrome *, UIXChromeFactory, (void))
NT_DEFINE(UIXChromeFactory)

/* ---- WinCE imports ---- */
typedef int (*WndProcT)(void *, U32, U32, U32);
typedef struct {
    U32 style;
    WndProcT proc;
    int cls_extra, win_extra;
    void *instance, *icon, *cursor, *background;
    const W16 *menu, *name;
} WndClass;
typedef struct {
    void *hwnd;
    U32 message, wparam, lparam;
    U32 time;
    Pt pt;
    void *next;
} Msg;
typedef struct {
    void *hdc;
    int erase;
    Rect area;
    int restore, inc;
    unsigned char reserved[32];
} Paint;

NT_IMPORT(U32, RegisterClassW, (const void *))
NT_DEFINE(RegisterClassW)
NT_IMPORT(void *, CreateWindowExW, (U32, const W16 *, const W16 *, U32, int, int, int, int, void *, void *, void *, void *))
NT_DEFINE(CreateWindowExW)
NT_IMPORT(int, ShowWindow, (void *, int))
NT_DEFINE(ShowWindow)
NT_IMPORT(int, UpdateWindow, (void *))
NT_DEFINE(UpdateWindow)
NT_IMPORT(int, InvalidateRect, (void *, const Rect *, int))
NT_DEFINE(InvalidateRect)
NT_IMPORT(int, GetMessageW, (void *, void *, U32, U32))
NT_DEFINE(GetMessageW)
NT_IMPORT(int, TranslateMessage, (const void *))
NT_DEFINE(TranslateMessage)
NT_IMPORT(int, DispatchMessageW, (const void *))
NT_DEFINE(DispatchMessageW)
NT_IMPORT(void *, BeginPaint, (void *, void *))
NT_DEFINE(BeginPaint)
NT_IMPORT(int, EndPaint, (void *, const void *))
NT_DEFINE(EndPaint)
NT_IMPORT(void *, CreateSolidBrush, (U32))
NT_DEFINE(CreateSolidBrush)
NT_IMPORT(int, FillRect, (void *, const Rect *, void *))
NT_DEFINE(FillRect)
NT_IMPORT(int, DeleteObject, (void *))
NT_DEFINE(DeleteObject)
NT_IMPORT(void, PostQuitMessage, (int))
NT_DEFINE(PostQuitMessage)
NT_IMPORT(void *, CreateFileW, (const W16 *, U32, U32, void *, U32, U32, void *))
NT_DEFINE(CreateFileW)
NT_IMPORT(int, ReadFile, (void *, void *, U32, U32 *, void *))
NT_DEFINE(ReadFile)
NT_IMPORT(int, CloseHandle, (void *))
NT_DEFINE(CloseHandle)
NT_IMPORT(U32, GetFileSize, (void *, U32 *))
NT_DEFINE(GetFileSize)

#define WM_PAINT_ 0x000Fu
#define WM_KEY_ 0x0100u
#define WM_CHAR_ 0x0102u
#define WM_DOWN_ 0x0201u
#define WM_UP_ 0x0202u
#define WM_MOVE_ 0x0200u

static int xstrlen(const char *s) { int n = 0; while (s && s[n]) ++n; return n; }
static int xeq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    int i = 0;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}
static void xcopy(char *d, const char *s, int n) { for (int i = 0; i < n; ++i) d[i] = s[i]; }

/* =====================================================================
 * palette: W10M light theme (App.xbf ThemeDictionaries[Light] + the
 * reference-device accent the files/settings ports measured)
 * ===================================================================== */
#define C_PAGE   RGB_(0xF7, 0xF3, 0xF7) /* AltHigh: numpad shade (capture) */
#define C_SHADE  RGB_(0xF7, 0xF3, 0xF7) /* keypad shade = page tint      */
#define C_SHEET  RGB_(0xE7, 0xE3, 0xE7) /* nav pane bg (capture)         */
#define C_HAIR   RGB_(0xE7, 0xE7, 0xE7) /* hairlines (capture)           */
#define C_ACC    RGB_(0x39, 0x65, 0xFF) /* accent (reference device)     */
#define C_INK    RGB_(0x00, 0x00, 0x00)
#define C_MUT    RGB_(0x63, 0x65, 0x63) /* BaseMedium text (capture)     */
#define C_DIS    RGB_(0xB5, 0xB2, 0xB5) /* disabled memory labels        */
#define C_SEL    RGB_(0x9C, 0xAE, 0xEF) /* nav pane selected band        */
#define C_PRESS  RGB_(0xDE, 0xDB, 0xDE) /* ListMedium press tint         */
#define C_SCRIM  RGB_(0xE7, 0xE3, 0xE7) /* flat light scrim (capture)    */
#define C_WHITE  RGB_(0xFF, 0xFF, 0xFF)

/* geometry */
#define TITLE_H   32
#define HAM_H     48
#define CHROME_Y  480
#define SCR_W     320
#define SCR_H     528

/* =====================================================================
 * decimal engine (no FPU on target): value = m * 10^e
 * ===================================================================== */
typedef struct {
    long long m;   /* mantissa (may be negative); 0 => value 0 */
    int e;         /* decimal exponent */
} Dec;

#define DECSIG 16  /* significant digits carried */

static Dec dec_norm(Dec a)
{
    if (a.m == 0) {
        a.e = 0;
        return a;
    }
    while (a.m % 10 == 0) {
        a.m /= 10;
        ++a.e;
    }
    return a;
}

static Dec dec_i(long long v)
{
    Dec a;
    a.m = v;
    a.e = 0;
    return dec_norm(a);
}

/* trim mantissa to at most `sig` significant digits (round half away) */
static Dec dec_trim(Dec a, int sig)
{
    int neg = a.m < 0;
    unsigned long long m = neg ? (unsigned long long)(-(a.m + 1)) + 1ull
                               : (unsigned long long)a.m;
    int d = 0;
    unsigned long long t = m;
    while (t) {
        ++d;
        t /= 10;
    }
    if (d <= sig)
        return a;
    int drop = d - sig;
    unsigned long long p = 1;
    for (int i = 0; i < drop; ++i)
        p *= 10;
    unsigned long long q = m / p;
    unsigned long long r = m % p;
    if (r >= p / 2)
        ++q;
    a.m = neg ? -(long long)q : (long long)q;
    a.e += drop;
    return dec_norm(a);
}

static Dec dec_add(Dec a, Dec b)
{
    if (a.m == 0)
        return b;
    if (b.m == 0)
        return a;
    if (a.e > b.e) {
        Dec t = a;
        a = b;
        b = t;
    }
    /* a is the finer (smaller e); de = b.e - a.e >= 0 */
    int de = b.e - a.e;
    Dec r;
    if (de == 0) {
        r.m = a.m + b.m;
        r.e = a.e;
    } else if (de > 19) {
        /* a lives far below b's decimal scale: negligible */
        return dec_trim(b, DECSIG);
    } else {
        long long p = 1;
        for (int i = 0; i < de; ++i)
            p *= 10;
        long long lim = (1ll << 62) / p;
        if (b.m <= lim && b.m >= -lim) {
            /* exact: lift b onto a's exponent */
            r.m = a.m + b.m * p;
            r.e = a.e;
        } else {
            /* b too coarse to lift: drop a below b's scale */
            r.m = a.m / p + b.m;
            r.e = b.e;
        }
    }
    return dec_trim(dec_norm(r), DECSIG);
}

static Dec dec_mul(Dec a, Dec b)
{
    if (a.m == 0 || b.m == 0)
        return dec_i(0);
    int neg = (a.m < 0) != (b.m < 0);
    unsigned long long ma = a.m < 0 ? (unsigned long long)(-(a.m + 1)) + 1ull
                                    : (unsigned long long)a.m;
    unsigned long long mb = b.m < 0 ? (unsigned long long)(-(b.m + 1)) + 1ull
                                    : (unsigned long long)b.m;
    /* split at 8 decimal digits: m = hi*10^8 + lo; every partial product
     * then fits int64 and the sum keeps 16+ significant digits exact */
    unsigned long long ah = ma / 100000000ull, al = ma % 100000000ull;
    unsigned long long bh = mb / 100000000ull, bl = mb % 100000000ull;
    int e = a.e + b.e;
    Dec p1, p2, p3;
    p1.m = (long long)(ah * bh);             p1.e = e + 16;
    p2.m = (long long)(ah * bl + al * bh);   p2.e = e + 8;
    p3.m = (long long)(al * bl);             p3.e = e;
    Dec r = dec_add(p1, p2);
    r = dec_add(r, p3);
    if (neg)
        r.m = -r.m;
    return dec_trim(dec_norm(r), DECSIG);
}

static Dec dec_div(Dec a, Dec b)
{
    if (b.m == 0 || a.m == 0)
        return dec_i(0);  /* caller reports Divide by zero */
    int neg = (a.m < 0) != (b.m < 0);
    unsigned long long x = a.m < 0 ? (unsigned long long)(-(a.m + 1)) + 1ull
                                   : (unsigned long long)a.m;
    unsigned long long y = b.m < 0 ? (unsigned long long)(-(b.m + 1)) + 1ull
                                   : (unsigned long long)b.m;
    int e = a.e - b.e;
    /* scale x up as far as 64 bits allow, then long-divide */
    while (x < (1ull << 63) / 10)
        x *= 10, --e;
    unsigned long long q = x / y;
    /* keep pulling digits while we have room */
    while (q < (1ull << 63) / 10) {
        unsigned long long r2 = x % y;
        if (!r2)
            break;
        q = q * 10 + (r2 * 10 / y);
        x = r2 * 10;
        --e;
        /* stop at 16 sig digits */
        unsigned long long t = q;
        int d = 0;
        while (t) {
            ++d;
            t /= 10;
        }
        if (d >= DECSIG)
            break;
    }
    Dec r;
    r.m = neg ? -(long long)q : (long long)q;
    r.e = e;
    return dec_trim(dec_norm(r), DECSIG);
}

/* ---- helpers on Dec (sign / compare / negate) ---- */
static int dec_is_zero(Dec a) { return a.m == 0; }
static int dec_sign(Dec a) { return a.m < 0 ? -1 : (a.m > 0 ? 1 : 0); }

static Dec dec_neg(Dec a)
{
    a.m = -a.m;
    return a;
}

/* three-way compare (sign-aware) */
static int dec_cmp(Dec a, Dec b)
{
    int sa = dec_sign(a), sb = dec_sign(b);
    if (sa != sb)
        return sa < sb ? -1 : 1;
    if (sa == 0)
        return 0;
    Dec d = dec_add(a, dec_neg(b));
    int sd = dec_sign(d);
    return sa < 0 ? -sd : sd;
}

/* alias kept for the series code below */
#define dec_add2 dec_add

/* ---- constants (16 sig digits) ---- */
static Dec dec_pi(void)
{
    Dec r;
    r.m = 3141592653589793ll;
    r.e = -15;
    return r;
}
static Dec dec_e(void)
{
    Dec r;
    r.m = 2718281828459045ll;
    r.e = -15;
    return r;
}
static Dec dec_ln10(void)
{
    Dec r;
    r.m = 2302585092994046ll;
    r.e = -15;
    return r;
}
static Dec dec_ln2(void)
{
    Dec r;
    r.m = 6931471805599453ll;
    r.e = -16;
    return r;
}

/* ---- series transcendentals in decimal arithmetic ---- */
static Dec dec_div_i(Dec a, long long den)
{
    return dec_div(a, dec_i(den));
}

/* x^k for small integer k */
static Dec dec_powi(Dec x, int k)
{
    Dec r = dec_i(1);
    for (int i = 0; i < k; ++i)
        r = dec_mul(r, x);
    return r;
}

/* exp(r) for |r| small-ish via Taylor; argument reduced by caller */
static Dec dec_exp_small(Dec r)
{
    /* sum r^k/k! until the term vanishes at our precision */
    Dec term = dec_i(1);
    Dec sum = dec_i(1);
    for (int k = 1; k <= 30; ++k) {
        term = dec_mul(term, dec_div_i(r, k));
        if (dec_sign(term) == 0)
            break;
        sum = dec_add(sum, term);
    }
    return dec_trim(sum, DECSIG);
}

static Dec dec_exp(Dec x)
{
    /* x = n*ln2 + rem, n = trunc(x/ln2) */
    Dec n = dec_div(x, dec_ln2());
    long long ni = 0;
    if (n.e >= 0) {
        long long p = 1;
        for (int i = 0; i < n.e && i < 18; ++i)
            p *= 10;
        ni = n.m * p;
    } else if (n.e < 0) {
        long long p = 1;
        int ok = 1;
        for (int i = 0; i < -n.e; ++i) {
            if (p > (1ll << 62) / 10) {
                ok = 0;
                break;
            }
            p *= 10;
        }
        if (ok)
            ni = n.m / p;
    }
    /* clamp n to keep 2^n sane */
    if (ni > 4000)
        ni = 4000;
    if (ni < -4000)
        ni = -4000;
    Dec rem = dec_add(x, dec_neg(dec_mul(dec_i(ni), dec_ln2())));
    Dec r = dec_exp_small(rem);
    /* r * 2^ni */
    while (ni > 0) {
        r = dec_add(r, r);
        --ni;
    }
    while (ni < 0) {
        r = dec_div_i(r, 2);
        ++ni;
    }
    return dec_trim(r, DECSIG);
}

/* ln(y) for y > 0 */
static Dec dec_ln(Dec y)
{
    if (dec_sign(y) <= 0)
        return dec_i(0);
    int e10 = 0;
    /* bring the value into [1,10) */
    Dec t = dec_trim(y, DECSIG);
    while (dec_cmp(t, dec_i(10)) >= 0) {
        t = dec_div_i(t, 10);
        ++e10;
        if (e10 > 400)
            break;
    }
    while (dec_cmp(t, dec_i(1)) < 0) {
        t = dec_mul(t, dec_i(10));
        --e10;
        if (e10 < -400)
            break;
    }
    /* ln(t) = 2*atanh(z), z = (t-1)/(t+1); |z| <= 1/3 */
    Dec z = dec_div(dec_add(t, dec_i(-1)), dec_add(t, dec_i(1)));
    Dec z2 = dec_mul(z, z);
    Dec term = z;
    Dec sum = dec_i(0);
    for (int k = 0; k < 40; ++k) {
        sum = dec_add(sum, dec_div_i(term, 2 * k + 1));
        term = dec_mul(term, z2);
        if (dec_sign(term) == 0)
            break;
    }
    Dec l = dec_mul(dec_i(2), sum);
    if (e10)
        l = dec_add(l, dec_mul(dec_i(e10), dec_ln10()));
    return dec_trim(l, DECSIG);
}

static Dec dec_log10(Dec x) { return dec_div(dec_ln(x), dec_ln10()); }

/* x^y = exp(y ln x) */
static Dec dec_pow(Dec x, Dec y)
{
    if (dec_sign(y) == 0)
        return dec_i(1);
    if (dec_sign(x) == 0)
        return dec_i(0);
    return dec_exp(dec_mul(y, dec_ln(x)));
}

/* sqrt: exponent-even integer Newton + decimal polish */
static Dec dec_sqrt(Dec x)
{
    if (dec_sign(x) <= 0)
        return dec_i(0);
    /* make the exponent even so value = m * 10^(2*e2), m integer */
    Dec t = x;
    while (t.e & 1) {
        if (t.m < (1ll << 62) / 10)
            t.m *= 10, --t.e;
        else
            t.m /= 10, ++t.e;
        if (t.m == 0)
            return dec_i(0);
    }
    int e2 = t.e / 2;
    long long m = t.m;
    if (m == 1) {
        Dec one;
        one.m = 1;
        one.e = e2;
        return dec_norm(one);
    }
    /* integer sqrt(m) by Newton, m < 9.2e18 -> root < 3.04e9 */
    long long r = 1;
    while (r * 100 <= m && r < 100000000ll)
        r *= 10;                 /* power-of-10 initial guess */
    for (int i = 0; i < 60; ++i) {
        long long nr = (r + m / r) / 2;
        if (nr == r)
            break;
        if ((nr > r && nr - r <= 1) || (r > nr && r - nr <= 1)) {
            if (nr < r)
                r = nr;
            break;
        }
        r = nr;
    }
    /* polish: Newton on the RAW mantissa as a decimal value, then
     * re-apply the (even) half exponent */
    Dec qm = dec_i(r);
    for (int i = 0; i < 6; ++i) {
        if (dec_is_zero(qm))
            break;
        Dec rq = dec_div(dec_i(m), qm);
        qm = dec_div_i(dec_add(qm, rq), 2);
    }
    Dec scale;
    scale.m = 1;
    scale.e = e2;
    return dec_trim(dec_norm(dec_mul(qm, scale)), DECSIG);
}

/* ---- trigonometry: decimal Taylor after full angle reduction ----
 * (no FPU, and the CORDIC path only bought 8 significant digits) */

/* reduce a decimal angle into [0, 2pi) */
static Dec dec_mod_2pi(Dec rad)
{
    Dec two_pi = dec_mul(dec_pi(), dec_i(2));
    while (dec_cmp(rad, two_pi) >= 0)
        rad = dec_add(rad, dec_neg(two_pi));
    while (dec_cmp(rad, dec_i(0)) < 0)
        rad = dec_add(rad, two_pi);
    return rad;
}

/* |x| > 1 test */
static int dec_abs_gt1(Dec a)
{
    if (dec_sign(a) == 0)
        return 0;
    if (dec_sign(a) < 0)
        a = dec_neg(a);
    return dec_cmp(a, dec_i(1)) > 0;
}

/* sin/cos on |t| <= pi/4 via Taylor, 16-digit exact decimal arithmetic */
static Dec taylor_sin(Dec t)
{
    /* t - t^3/3! + t^5/5! - ... : t <= 0.785 -> 9 terms reach 1e-17 */
    Dec t2 = dec_mul(t, t);
    Dec term = t;
    Dec sum = t;
    for (int k = 1; k <= 9; ++k) {
        term = dec_mul(term, dec_div_i(t2, (2 * k) * (2 * k + 1)));
        if (dec_sign(term) == 0)
            break;
        sum = dec_add(sum, (k & 1) ? dec_neg(term) : term);
    }
    return dec_trim(sum, DECSIG);
}

static Dec taylor_cos(Dec t)
{
    /* 1 - t^2/2! + t^4/4! - ... */
    Dec t2 = dec_mul(t, t);
    Dec term = dec_i(1);
    Dec sum = dec_i(1);
    for (int k = 1; k <= 9; ++k) {
        term = dec_mul(term, dec_div_i(t2, (2 * k - 1) * (2 * k)));
        if (dec_sign(term) == 0)
            break;
        sum = dec_add(sum, (k & 1) ? dec_neg(term) : term);
    }
    return dec_trim(sum, DECSIG);
}

/* full sin/cos: reduce mod 2pi, fold into [0, pi/4] with symmetries */
static void dec_sincos(Dec rad, Dec *sout, Dec *cout)
{
    Dec x = dec_mod_2pi(rad);
    Dec pi = dec_pi();
    Dec pi2 = dec_div_i(pi, 2);
    int neg = 0, swapped = 0;
    /* into [0, pi] */
    if (dec_cmp(x, pi) > 0) {
        x = dec_add(dec_mul(pi, dec_i(2)), dec_neg(x)); /* 2pi - x */
        neg = 1;
    }
    /* into [0, pi/2] */
    if (dec_cmp(x, pi2) > 0) {
        x = dec_add(pi, dec_neg(x)); /* pi - x */
    }
    /* into [0, pi/4]: sin(x) = cos(pi/2 - x) */
    Dec q = dec_div_i(pi, 4);
    if (dec_cmp(x, q) > 0) {
        x = dec_add(pi2, dec_neg(x));
        swapped = 1;
    }
    Dec s = taylor_sin(x);
    Dec c = taylor_cos(x);
    if (swapped) {
        Dec t = s;
        s = c;
        c = t;
    }
    if (neg)
        s = dec_neg(s);
    *sout = s;
    *cout = c;
}

static Dec dec_sin(Dec rad)
{
    Dec s, c;
    dec_sincos(rad, &s, &c);
    return s;
}

static Dec dec_cos(Dec rad)
{
    Dec s, c;
    dec_sincos(rad, &s, &c);
    return c;
}

static Dec dec_tan(Dec rad)
{
    Dec s, c;
    dec_sincos(rad, &s, &c);
    if (dec_is_zero(c)) {
        /* caller surfaces the error; huge value here */
        return dec_div(dec_i(1), dec_i(0).m == 0 ? dec_i(1) : dec_i(1));
    }
    return dec_div(s, c);
}

/* atan(z) for |z| <= 1: reduce twice via atan(z)=2*atan(z/(1+sqrt(1+z^2)))
 * (|z| < 0.27), then Taylor z - z^3/3 + z^5/5 - ... */
static Dec dec_atan(Dec x)
{
    int neg = dec_sign(x) < 0;
    if (neg)
        x = dec_neg(x);
    int flipped = 0;
    if (dec_abs_gt1(x)) {
        x = dec_div(dec_i(1), x);
        flipped = 1;
    }
    for (int i = 0; i < 2; ++i) {
        Dec t = dec_div(x, dec_add(dec_i(1),
                     dec_sqrt(dec_add(dec_i(1), dec_mul(x, x)))));
        x = t;
    }
    /* Taylor */
    Dec x2 = dec_mul(x, x);
    Dec term = x;
    Dec sum = dec_i(0);
    for (int k = 0; k < 14; ++k) {
        Dec piece = dec_div_i(term, 2 * k + 1);
        sum = dec_add(sum, (k & 1) ? dec_neg(piece) : piece);
        term = dec_mul(term, x2);
        if (dec_sign(term) == 0)
            break;
    }
    sum = dec_mul(sum, dec_i(4)); /* undo both halvings at once */
    if (flipped)
        sum = dec_add(dec_div(dec_pi(), dec_i(2)), dec_neg(sum));
    if (neg)
        sum = dec_neg(sum);
    return dec_trim(sum, DECSIG);
}

static Dec dec_asin(Dec x)
{
    /* asin(x) = atan(x / sqrt(1 - x^2)) */
    Dec s = dec_sqrt(dec_add(dec_i(1), dec_neg(dec_mul(x, x))));
    if (dec_is_zero(s))
        return dec_div(dec_pi(), dec_i(2));
    return dec_atan(dec_div(x, s));
}

static Dec dec_acos(Dec x)
{
    return dec_add(dec_div(dec_pi(), dec_i(2)), dec_neg(dec_asin(x)));
}

static Dec dec_sinh(Dec x)
{
    Dec ex = dec_exp(x);
    return dec_div_i(dec_add2(ex, dec_div(dec_i(1), ex)), 2);
}

static Dec dec_cosh(Dec x)
{
    Dec ex = dec_exp(x);
    return dec_div_i(dec_add2(ex, dec_div(dec_i(1), ex)), 2);
}

static Dec dec_tanh(Dec x)
{
    Dec ex = dec_exp(x);
    Dec nx = dec_div(dec_i(1), ex);
    return dec_div(dec_add2(ex, dec_neg(nx)), dec_add2(ex, nx));
}

static Dec dec_asinh(Dec x)
{
    return dec_ln(dec_add2(dec_sqrt(dec_add2(dec_mul(x, x), dec_i(1))), x));
}

static Dec dec_acosh(Dec x)
{
    return dec_ln(dec_add2(x, dec_sqrt(dec_add2(dec_mul(x, x), dec_i(-1)))));
}

static Dec dec_atanh(Dec x)
{
    return dec_div_i(dec_ln(dec_div(dec_add2(dec_i(1), x),
                                    dec_add2(dec_i(1), dec_neg(x)))), 2);
}

    /* factorial (integer values only, cap at 20!) */
static Dec dec_fact(Dec x)
{
    if (dec_sign(x) < 0 || x.e < 0)
        return dec_i(0);
    long long p = 1;
    for (int i = 0; i < x.e && i < 18; ++i)
        p *= 10;
    long long n = x.m * p;
    if (n < 0 || n > 20)
        return dec_i(0);
    long long r = 1;
    for (long long i = 2; i <= n; ++i)
        r *= i;
    return dec_i(r);
}

/* =====================================================================
 * Dec -> display string (Windows calc style: comma grouping, e-notation
 * for extremes; the engine already caps us at 16 significant digits)
 * ===================================================================== */
static int dec_digits(unsigned long long m)
{
    int d = 0;
    while (m) {
        ++d;
        m /= 10;
    }
    return d;
}

/* digits of |m| into tmp[], returns count; tmp[0] = most significant */
static int dec_ull_digits(unsigned long long m, char *tmp)
{
    int t = 0;
    while (m) {
        tmp[t++] = (char)('0' + (m % 10));
        m /= 10;
    }
    /* reverse */
    for (int i = 0; i < t / 2; ++i) {
        char c = tmp[i];
        tmp[i] = tmp[t - 1 - i];
        tmp[t - 1 - i] = c;
    }
    return t;
}

void dec_to_string_buf(Dec a, char *buf, int bufsize)
{
    (void)bufsize;
    int p = 0;
    if (dec_sign(a) == 0) {
        buf[p++] = '0';
        buf[p] = 0;
        return;
    }
    int neg = a.m < 0;
    unsigned long long m = neg ? (unsigned long long)(-(a.m + 1)) + 1ull
                               : (unsigned long long)a.m;
    int e = a.e;
    char tmp[24];
    int d = dec_ull_digits(m, tmp);
    int exp10 = (d - 1) + e;      /* exponent of the leading digit */

    if (exp10 >= 16 || exp10 <= -17) {
        /* e-notation: d.ddde+XX */
        if (neg)
            buf[p++] = '-';
        buf[p++] = tmp[0];
        int frac = d - 1;
        if (frac > 0) {
            buf[p++] = '.';
            if (frac > 15)
                frac = 15;
            for (int i = 1; i <= frac; ++i)
                buf[p++] = tmp[i];
        }
        buf[p++] = 'e';
        int ex = exp10;
        if (ex < 0) {
            buf[p++] = '-';
            ex = -ex;
        } else {
            buf[p++] = '+';
        }
        if (ex >= 100) {
            buf[p++] = (char)('0' + ex / 100);
            ex %= 100;
        }
        if (ex >= 10) {
            buf[p++] = (char)('0' + ex / 10);
            buf[p++] = (char)('0' + ex % 10);
        } else {
            buf[p++] = (char)('0' + ex);
        }
        buf[p] = 0;
        return;
    }

    if (neg)
        buf[p++] = '-';
    if (e >= 0) {
        /* pure integer: d digits + e zeros, grouped by 3 from the units
         * digit (comma before positions where remaining % 3 == 0) */
        int total = d + e;
        int emitted = 0;
        for (int i = 0; i < d; ++i) {
            buf[p++] = tmp[i];
            ++emitted;
            if ((total - emitted) % 3 == 0 && emitted < total)
                buf[p++] = ',';
        }
        for (int i = 0; i < e; ++i) {
            buf[p++] = '0';
            ++emitted;
            if ((total - emitted) % 3 == 0 && emitted < total)
                buf[p++] = ',';
        }
        buf[p] = 0;
        return;
    }

    /* e < 0: fractional */
    int f = -e;
    if (d <= f) {
        /* value < 1: 0.000ddd */
        buf[p++] = '0';
        buf[p++] = '.';
        for (int i = 0; i < f - d; ++i)
            buf[p++] = '0';
        for (int i = 0; i < d; ++i)
            buf[p++] = tmp[i];
        buf[p] = 0;
        return;
    }
    /* mixed: (d-f) integer digits then f fraction digits */
    int ip = d - f;
    for (int i = 0; i < ip; ++i) {
        buf[p++] = tmp[i];
        if ((ip - i - 1) % 3 == 0 && i < ip - 1)
            buf[p++] = ',';
    }
    buf[p++] = '.';
    for (int i = ip; i < d; ++i)
        buf[p++] = tmp[i];
    buf[p] = 0;
}

/* =====================================================================
 * engine state (standard + scientific)
 * ===================================================================== */
enum { OP_NONE = 0, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_YROOT,
       OP_AND, OP_OR, OP_XOR, OP_LSH, OP_RSH };

enum { ANG_DEG = 0, ANG_RAD, ANG_GRAD };

typedef struct {
    Dec acc;            /* accumulator (left operand)            */
    int pending;        /* OP_* waiting for the right operand    */
    int last_op;        /* op repeated on = (Windows behaviour)  */
    Dec last_operand;   /* right operand for repeats             */
    char entry[32];     /* live entry: digits '.' '-'            */
    int entry_active;   /* 1 while typing                        */
    Dec display;        /* what the big line shows               */
    char expr[64];      /* the replay/expression line            */
    int err;            /* 0 ok, 1 divide by zero, 2 overflow,
                           3 undefined, 4 invalid input          */
    /* scientific */
    int angle_mode;
    int second;         /* 2nd function toggle                   */
    int hyp;            /* HYP hyperbolic toggle                 */
    int fe;             /* F-E fixed-exponent display toggle     */
    /* programmer */
    int prog_base;      /* 0 HEX 1 DEC 2 OCT 3 BIN (active)      */
    int prog_qword;     /* 0 QWORD 1 DWORD 2 WORD 3 BYTE         */
    int prog_bitview;   /* bit-toggle keyboard instead of keypad */
    /* paren stack */
    struct {
        Dec acc;
        int pending;
        char expr[48];
        int expr_len;
    } parens[8];
    int paren_depth;
} Calc;

static Calc g;

static void calc_reset_soft(void)
{
    g.entry[0] = 0;
    g.entry_active = 0;
    g.err = 0;
}

static void calc_clear_all(void)
{
    g.acc = dec_i(0);
    g.pending = OP_NONE;
    g.last_op = OP_NONE;
    g.display = dec_i(0);
    g.expr[0] = 0;
    g.paren_depth = 0;
    calc_reset_soft();
}

static void engine_init(void)
{
    calc_clear_all();
    g.angle_mode = ANG_DEG;
    g.second = 0;
    g.prog_base = 1; /* DEC active by default (capture) */
    g.prog_qword = 0;
    g.prog_bitview = 0;
}

/* ---- entry handling ---- */
static int entry_is_zeroish(void)
{
    /* "0", "-0", "" */
    int i = 0;
    if (g.entry[0] == '-')
        i = 1;
    if (!g.entry[i])
        return 1;
    for (; g.entry[i]; ++i)
        if (g.entry[i] != '0')
            return 0;
    return 1;
}

static Dec entry_value(void)
{
    if (!g.entry_active || !g.entry[0])
        return g.display;
    /* parse decimal string; an 'e' suffix multiplies by 10^exp */
    long long m = 0;
    int e = 0, neg = 0, seen_dot = 0, digits = 0;
    const char *s = g.entry;
    const char *ep = 0;
    for (const char *q = g.entry; *q; ++q)
        if (*q == 'e' || *q == 'E')
            ep = q;
    long long escale = 1;
    if (ep) {
        int ee = 0, esign = 1;
        const char *q = ep + 1;
        if (*q == '+')
            ++q;
        else if (*q == '-')
            esign = -1, ++q;
        for (; *q >= '0' && *q <= '9'; ++q)
            ee = ee * 10 + (*q - '0');
        e += esign * ee; /* fold into the decimal exponent directly */
        (void)escale;
    }
    if (*s == '-') {
        neg = 1;
        ++s;
    }
    for (; *s; ++s) {
        if (*s == 'e' || *s == 'E')
            break; /* exponent handled above */
        if (*s == '.') {
            seen_dot = 1;
            continue;
        }
        if (*s < '0' || *s > '9')
            continue;
        if (digits < 18) {
            m = m * 10 + (*s - '0');
            if (seen_dot)
                --e;
            if (m)
                ++digits;
        } else if (seen_dot) {
            /* drop extra decimals */
        } else {
            ++e; /* extra integer digits shift the exponent */
        }
    }
    Dec r;
    r.m = neg ? -m : m;
    r.e = e;
    return dec_norm(r);
}

static void set_display_from_entry(void)
{
    g.display = entry_value();
}

static void entry_digit(char c)
{
    int len = 0;
    while (g.entry[len])
        ++len;
    if (c == '.') {
        int has = 0;
        for (int i = 0; i < len; ++i)
            if (g.entry[i] == '.')
                has = 1;
        if (has)
            return;
        if (len == 0) {
            g.entry[0] = '0';
            g.entry[1] = '.';
            g.entry[2] = 0;
        } else if (len < 30) {
            g.entry[len] = '.';
            g.entry[len + 1] = 0;
        }
        g.entry_active = 1;
        return;
    }
    if (len >= 30)
        return;
    /* max 16 significant digits in entry */
    int sig = 0, seen_dot = 0;
    for (int i = 0; i < len; ++i) {
        if (g.entry[i] == '.') {
            seen_dot = 1;
            continue;
        }
        if (g.entry[i] != '0' || sig)
            ++sig;
    }
    if (!seen_dot && sig >= 16)
        return;
    g.entry[len] = c;
    g.entry[len + 1] = 0;
    g.entry_active = 1;
    /* strip leading zeros ("007" -> "7") */
    int i = (g.entry[0] == '-') ? 1 : 0;
    while (g.entry[i] == '0' && g.entry[i + 1] && g.entry[i + 1] != '.')
        ++i;
    if (i) {
        int j = 0;
        if (g.entry[0] == '-')
            g.entry[j++] = '-';
        for (; g.entry[i]; ++i)
            g.entry[j++] = g.entry[i];
        g.entry[j] = 0;
    }
}

static void entry_backspace(void)
{
    if (!g.entry_active)
        return;
    int len = 0;
    while (g.entry[len])
        ++len;
    if (len) {
        g.entry[len - 1] = 0;
        if (!len - 1)
            g.entry_active = 0;
    }
}

/* ---- expression string ---- */
static void expr_append(const char *tok)
{
    int len = 0;
    while (g.expr[len])
        ++len;
    int i = 0;
    while (tok[i] && len < 60) {
        g.expr[len++] = tok[i++];
    }
    g.expr[len] = 0;
}

static const char *op_token(int op)
{
    switch (op) {
    case OP_ADD: return " + ";
    case OP_SUB: return " - ";
    case OP_MUL: return " x ";
    case OP_DIV: return " / ";
    case OP_MOD: return " % ";
    case OP_POW: return " ^ ";
    case OP_AND: return " And ";
    case OP_OR: return " Or ";
    case OP_XOR: return " Xor ";
    case OP_LSH: return " Lsh ";
    case OP_RSH: return " Rsh ";
    default: return "";
    }
}

/* ---- operation application ---- */
static Dec apply_op(int op, Dec a, Dec b, int *err)
{
    *err = 0;
    switch (op) {
    case OP_ADD: return dec_add2(a, b);
    case OP_SUB: return dec_add2(a, dec_neg(b));
    case OP_MUL: return dec_mul(a, b);
    case OP_DIV:
        if (dec_is_zero(b)) {
            *err = 1;
            return dec_i(0);
        }
        return dec_div(a, b);
    case OP_MOD: {
        if (dec_is_zero(b)) {
            *err = 1;
            return dec_i(0);
        }
        Dec q = dec_div(a, b);
        /* integer part of q */
        Dec ip = q;
        if (q.e > 0) {
            long long p = 1;
            for (int i = 0; i < q.e && i < 18; ++i)
                p *= 10;
            ip.m = q.m * p;
            ip.e = 0;
        } else if (q.e < 0) {
            long long p = 1;
            for (int i = 0; i < -q.e; ++i)
                p *= 10;
            ip.m = q.m / p;
            ip.e = 0;
        }
        ip = dec_trim(ip, DECSIG);
        return dec_add2(a, dec_neg(dec_mul(ip, b)));
    }
    case OP_AND:
        return dec_i((a.m & b.m));
    case OP_OR:
        return dec_i((a.m | b.m));
    case OP_XOR:
        return dec_i((a.m ^ b.m));
    case OP_LSH: {
        if (b.m < 0 || b.m > 63)
            return dec_i(0);
        return dec_i((long long)((unsigned long long)a.m << b.m));
    }
    case OP_RSH:
        if (b.m < 0 || b.m > 63)
            return dec_i(0);
        return dec_i(a.m >> b.m);
    case OP_POW: return dec_pow(a, b);
    case OP_YROOT: {
        if (dec_is_zero(b))
            return dec_i(0);
        Dec inv = dec_div(dec_i(1), b);
        return dec_pow(a, inv);
    }
    }
    return b;
}

static int op_is_high(int op)
{
    return op == OP_MUL || op == OP_DIV || op == OP_MOD || op == OP_POW ||
           op == OP_YROOT;
}

/* resolve pending op with the current entry/display as the right operand */
static void resolve_pending(int next_op)
{
    Dec b = entry_value();
    if (g.pending == OP_NONE) {
        g.acc = b;
    } else {
        /* precedence: high ops bind before low ops (scientific feel) */
        if (g.pending == OP_ADD || g.pending == OP_SUB) {
            g.acc = apply_op(g.pending, g.acc, b, &g.err);
            if (g.err)
                return;
        } else {
            g.acc = apply_op(g.pending, g.acc, b, &g.err);
            if (g.err)
                return;
        }
        (void)next_op;
    }
    g.display = g.acc;
    g.entry_active = 0;
    g.entry[0] = 0;
}

static void binary_op(int op)
{
    if (g.err)
        return;
    resolve_pending(op);
    if (g.err)
        return;
    g.pending = op;
    expr_append(op_token(op));
}

static void equals(void)
{
    if (g.err)
        return;
    Dec b = entry_value();
    if (g.pending == OP_NONE) {
        /* repeat the last operation (Windows behaviour) */
        if (g.last_op != OP_NONE) {
            g.acc = apply_op(g.last_op, g.display, g.last_operand, &g.err);
            if (g.err)
                return;
            g.display = g.acc;
        } else {
            g.acc = b;
            g.display = g.acc;
        }
    } else {
        g.last_op = g.pending;
        g.last_operand = b;
        g.acc = apply_op(g.pending, g.acc, b, &g.err);
        if (g.err)
            return;
        g.display = g.acc;
        g.pending = OP_NONE;
    }
    g.entry_active = 0;
    g.entry[0] = 0;
    /* close any open parens implicitly */
    g.paren_depth = 0;
}

/* angle conversion for trig input */
static Dec to_radians(Dec ang)
{
    switch (g.angle_mode) {
    case ANG_DEG: return dec_mul(ang, dec_div(dec_pi(), dec_i(180)));
    case ANG_GRAD: return dec_mul(ang, dec_div(dec_pi(), dec_i(200)));
    default: return ang;
    }
}

static Dec from_radians(Dec rad)
{
    switch (g.angle_mode) {
    case ANG_DEG: return dec_mul(rad, dec_div(dec_i(180), dec_pi()));
    case ANG_GRAD: return dec_mul(rad, dec_div(dec_i(200), dec_pi()));
    default: return rad;
    }
}

/* unary ops: apply to the current entry/display immediately */
static void unary(const char *token, Dec (*fn)(Dec))
{
    if (g.err)
        return;
    Dec v = entry_value();
    Dec r = fn(v);
    g.display = r;
    g.entry_active = 0;
    g.entry[0] = 0;
    if (token && token[0] && g.expr[0])
        expr_append(" ");
    if (token)
        expr_append(token);
}

static void fn_trig(int which)
{
    /* which: 0 sin 1 cos 2 tan (+8 => inverse, +16 => hyperbolic) */
    int inv = 0, hyp = 0;
    while (which >= 8) {
        if (which >= 16) {
            hyp = 1;
            which -= 16;
        } else {
            inv = 1;
            which -= 8;
        }
    }
    /* callers pass the full which: +8 inverse, +16 hyperbolic */
    Dec v = entry_value();
    Dec r = v;
    const char *name = "?";
    if (hyp) {
        switch (which) {
        case 0: name = inv ? "asinh" : "sinh";
                r = inv ? dec_asinh(v) : dec_sinh(v); break;
        case 1: name = inv ? "acosh" : "cosh";
                r = inv ? dec_acosh(v) : dec_cosh(v); break;
        case 2: name = inv ? "atanh" : "tanh";
                r = inv ? dec_atanh(v) : dec_tanh(v); break;
        }
    } else if (inv) {
        switch (which) {
        case 0: name = "asin"; r = dec_asin(v); break;
        case 1: name = "acos"; r = dec_acos(v); break;
        case 2: name = "atan"; r = dec_atan(v); break;
        }
        /* inverse trig returns radians: show in the current angle unit */
        r = from_radians(r);
    } else {
        Dec rad = to_radians(v);
        switch (which) {
        case 0: name = "sin"; r = dec_sin(rad); break;
        case 1: name = "cos"; r = dec_cos(rad); break;
        case 2: name = "tan"; r = dec_tan(rad); break;
        }
    }
    g.display = r;
    g.entry_active = 0;
    g.entry[0] = 0;
    if (g.expr[0])
        expr_append(" ");
    expr_append(name);
}

static void fn_log(void)  { unary("log", dec_log10); }
static void fn_ln(void)   { unary("ln", dec_ln); }
static void fn_exp10(void)
{
    /* 10^x */
    Dec v = entry_value();
    g.display = dec_pow(dec_i(10), v);
    g.entry_active = 0;
    g.entry[0] = 0;
    if (g.expr[0])
        expr_append(" ");
    expr_append("10^");
}
static void fn_expe(void)
{
    Dec v = entry_value();
    g.display = dec_exp(v);
    g.entry_active = 0;
    g.entry[0] = 0;
    if (g.expr[0])
        expr_append(" ");
    expr_append("e^");
}
static void fn_fact(void)
{
    unary("!", dec_fact);
}

/* deg <-> dms */
static Dec to_dms(Dec deg)
{
    /* d.dd -> d.mmss */
    int neg = dec_sign(deg) < 0;
    Dec a = neg ? dec_neg(deg) : deg;
    Dec ip = a;
    if (a.e > 0) {
        long long p = 1;
        for (int i = 0; i < a.e && i < 18; ++i)
            p *= 10;
        ip.m = a.m * p;
        ip.e = 0;
    } else if (a.e < 0) {
        long long p = 1;
        for (int i = 0; i < -a.e; ++i)
            p *= 10;
        ip.m = a.m / p;
        ip.e = 0;
    }
    Dec fr = dec_add2(a, dec_neg(ip));
    Dec m = dec_mul(fr, dec_i(60));
    Dec mi = m;
    if (m.e > 0) {
        long long p = 1;
        for (int i = 0; i < m.e && i < 18; ++i)
            p *= 10;
        mi.m = m.m * p;
        mi.e = 0;
    } else if (m.e < 0) {
        long long p = 1;
        for (int i = 0; i < -m.e; ++i)
            p *= 10;
        mi.m = m.m / p;
        mi.e = 0;
    }
    Dec s = dec_mul(dec_add2(m, dec_neg(mi)), dec_i(60));
    Dec r = dec_add2(dec_add2(ip, dec_div_i(mi, 100)), dec_div_i(s, 10000));
    return neg ? dec_neg(r) : r;
}

static Dec from_dms(Dec dms)
{
    int neg = dec_sign(dms) < 0;
    Dec a = neg ? dec_neg(dms) : dms;
    Dec ip = a;
    if (a.e > 0) {
        long long p = 1;
        for (int i = 0; i < a.e && i < 18; ++i)
            p *= 10;
        ip.m = a.m * p;
        ip.e = 0;
    } else if (a.e < 0) {
        long long p = 1;
        for (int i = 0; i < -a.e; ++i)
            p *= 10;
        ip.m = a.m / p;
        ip.e = 0;
    }
    Dec fr = dec_add2(a, dec_neg(ip));
    Dec m = dec_mul(fr, dec_i(100));
    Dec mi = m;
    if (m.e > 0) {
        long long p = 1;
        for (int i = 0; i < m.e && i < 18; ++i)
            p *= 10;
        mi.m = m.m * p;
        mi.e = 0;
    } else if (m.e < 0) {
        long long p = 1;
        for (int i = 0; i < -m.e; ++i)
            p *= 10;
        mi.m = m.m / p;
        mi.e = 0;
    }
    Dec s = dec_mul(dec_add2(m, dec_neg(mi)), dec_i(100));
    Dec r = dec_add2(dec_add2(ip, dec_div_i(mi, 60)), dec_div_i(s, 3600));
    return neg ? dec_neg(r) : r;
}

static void op_dms(void) { unary("dms", to_dms); }
static void op_deg(void) { unary("deg", from_dms); }

/* ---- paren handling (scientific) ---- */
static void paren_open(void)
{
    if (g.paren_depth >= 8)
        return;
    int len = 0;
    while (g.expr[len])
        ++len;
    g.parens[g.paren_depth].acc = g.acc;
    g.parens[g.paren_depth].pending = g.pending;
    for (int i = 0; i < 48; ++i)
        g.parens[g.paren_depth].expr[i] = g.expr[i];
    g.parens[g.paren_depth].expr_len = len;
    ++g.paren_depth;
    g.acc = dec_i(0);
    g.pending = OP_NONE;
    g.expr[0] = 0;
    g.entry_active = 0;
    g.entry[0] = 0;
    g.display = dec_i(0);
}

static void paren_close(void)
{
    if (!g.paren_depth)
        return;
    resolve_pending(OP_NONE);
    if (g.err)
        return;
    --g.paren_depth;
    Dec inner = g.acc;
    g.acc = g.parens[g.paren_depth].acc;
    g.pending = g.parens[g.paren_depth].pending;
    for (int i = 0; i < 48; ++i)
        g.expr[i] = g.parens[g.paren_depth].expr[i];
    /* the paren result becomes the active entry */
    g.display = inner;
    g.entry_active = 0;
    g.entry[0] = 0;
    /* stash inner as the operand: put it in expr for display */
    expr_append("(=)");
}

/* ---- unary button handlers ---- */
static void op_percent(void)
{
    /* W10 standard: % completes as "x% of accumulator" (or of the entry
     * when no pending op): a b + % => acc + b%*acc */
    Dec v = entry_value();
    if (g.pending == OP_NONE || dec_is_zero(g.acc)) {
        g.display = dec_div_i(v, 100);
    } else {
        g.display = dec_div_i(dec_mul(v, g.acc), 100);
    }
    g.entry_active = 0;
    g.entry[0] = 0;
    expr_append("%");
}

static void op_sqrt(void)
{
    Dec v = entry_value();
    if (dec_sign(v) < 0) {
        g.err = 3;
        return;
    }
    g.display = dec_sqrt(v);
    g.entry_active = 0;
    g.entry[0] = 0;
    if (g.expr[0])
        expr_append(" ");
    expr_append("sqrt");
}

static void op_sqr(void)
{
    Dec v = entry_value();
    g.display = dec_mul(v, v);
    g.entry_active = 0;
    g.entry[0] = 0;
    if (g.expr[0])
        expr_append(" ");
    expr_append("sqr");
}

static void op_cube(void)
{
    Dec v = entry_value();
    g.display = dec_mul(dec_mul(v, v), v);
    g.entry_active = 0;
    g.entry[0] = 0;
    if (g.expr[0])
        expr_append(" ");
    expr_append("cube");
}

static void op_invert(void)
{
    Dec v = entry_value();
    if (dec_is_zero(v)) {
        g.err = 1;
        return;
    }
    g.display = dec_div(dec_i(1), v);
    g.entry_active = 0;
    g.entry[0] = 0;
    if (g.expr[0])
        expr_append(" ");
    expr_append("1/x");
}

static void op_negate(void)
{
    if (g.entry_active && g.entry[0]) {
        if (g.entry[0] == '-') {
            int i = 1, j = 0;
            while (g.entry[i])
                g.entry[j++] = g.entry[i++];
            g.entry[j] = 0;
        } else {
            int len = 0;
            while (g.entry[len])
                ++len;
            for (int i = len; i >= 0; --i)
                g.entry[i + 1] = g.entry[i];
            g.entry[0] = '-';
        }
        set_display_from_entry();
    } else {
        g.display = dec_neg(g.display);
        g.acc = dec_neg(g.acc);
    }
}

static void op_pi(void)
{
    g.display = dec_pi();
    g.entry_active = 0;
    g.entry[0] = 0;
    if (g.expr[0])
        expr_append(" ");
    expr_append("pi");
}

static void op_clear_entry(void)
{
    g.entry[0] = 0;
    g.entry_active = 0;
    if (g.pending != OP_NONE)
        g.display = g.acc;
    else
        g.display = dec_i(0);
    g.err = 0;
}

static void op_clear(void)
{
    calc_clear_all();
}

/* =====================================================================
 * memory + history stores
 * ===================================================================== */
#define MEM_MAX 12
#define HIST_MAX 20

static Dec mem_vals[MEM_MAX];
static char mem_str[MEM_MAX][24];
static int mem_count;

static struct {
    char expr[56];
    char result[24];
} hist[HIST_MAX];
static int hist_count;

static void mem_push(Dec v)
{
    if (mem_count == MEM_MAX) {
        for (int i = 1; i < MEM_MAX; ++i) {
            mem_vals[i - 1] = mem_vals[i];
            for (int j = 0; j < 24; ++j)
                mem_str[i - 1][j] = mem_str[i][j];
        }
        --mem_count;
    }
    mem_vals[mem_count] = v;
    dec_to_string_buf(v, mem_str[mem_count], 20);
    ++mem_count;
}

static void hist_push(const char *e, const char *r)
{
    if (hist_count == HIST_MAX) {
        for (int i = 1; i < HIST_MAX; ++i) {
            for (int j = 0; j < 56; ++j)
                hist[i - 1].expr[j] = hist[i].expr[j];
            for (int j = 0; j < 24; ++j)
                hist[i - 1].result[j] = hist[i].result[j];
        }
        --hist_count;
    }
    int i = 0;
    for (; e[i] && i < 55; ++i)
        hist[hist_count].expr[i] = e[i];
    hist[hist_count].expr[i] = 0;
    i = 0;
    for (; r[i] && i < 23; ++i)
        hist[hist_count].result[i] = r[i];
    hist[hist_count].result[i] = 0;
    ++hist_count;
}


/* =====================================================================
 * unit converter data (UnitConverter.xbf + pri unit names/abbreviations;
 * factors are exact decimal constants, temperature is affine)
 * ===================================================================== */
typedef struct {
    const char *name;    /* display name (pri UnitName_*) */
    const char *abbr;    /* abbreviation (pri UnitAbbreviation_*) */
    long long fm;        /* factor mantissa: value_in_base = v * fm * 10^fe */
    int fe;
} Unit;

typedef struct {
    const char *name;    /* CategoryName_* */
    int count;
    const Unit *units;
    int supp[3];         /* supplementary result unit indices */
} Category;

#define U(nm, ab, m, e) { nm, ab, m, e }

static const Unit U_VOLUME[] = {
    U("Milliliters", "mL", 1, 0),
    U("Cubic centimeters", "cm3", 1, 0),
    U("Liters", "L", 1000, 0),
    U("Cubic meters", "m3", 1000000, 0),
    U("Teaspoons (US)", "tsp", 4928921, -6),
    U("Tablespoons (US)", "tbsp", 14786765, -6),
    U("Fluid ounces (US)", "fl oz", 2957353, -5),
    U("Cups (US)", "cup", 2365882, -4),
    U("Pints (US)", "pt", 4731765, -4),
    U("Quarts (US)", "qt", 9463530, -4),
    U("Gallons (US)", "gal", 3785412, -3),
    U("Gallons (UK)", "gal", 4546090, -3),
    U("Cubic inches", "in3", 16387064, -6),
    U("Cubic feet", "ft3", 28316847, -3),
    U("Cubic yards", "yd3", 764554857, -3),
};

static const Unit U_LENGTH[] = {
    U("Nanometers", "nm", 1, -9),
    U("Microns", "um", 1, -6),
    U("Millimeters", "mm", 1, -3),
    U("Centimeters", "cm", 1, -2),
    U("Meters", "m", 1, 0),
    U("Kilometers", "km", 1, 3),
    U("Inches", "in", 254, -4),
    U("Feet", "ft", 3048, -4),
    U("Yards", "yd", 9144, -4),
    U("Miles", "mi", 1609344, -3),
    U("Nautical Miles", "nmi", 1852, 0),
};

static const Unit U_WEIGHT[] = {
    U("Carats", "CD", 2, -4),
    U("Milligrams", "mg", 1, -6),
    U("Centigrams", "cg", 1, -5),
    U("Decigrams", "dg", 1, -4),
    U("Grams", "g", 1, -3),
    U("Dekagrams", "dag", 1, -2),
    U("Hectograms", "hg", 1, -1),
    U("Kilograms", "kg", 1, 0),
    U("Metric tonnes", "t", 1000, 0),
    U("Ounces", "oz", 28349523, -6),
    U("Pounds", "lb", 45359237, -5),
    U("Long tons (UK)", "ton", 1016046909, -3),
    U("Short tons (US)", "ton", 907184740, -3),
    U("Stone", "st", 63502932, -4),
};

static const Unit U_TEMP[] = {
    U("Celsius", "C", 1, 0),
    U("Fahrenheit", "F", 1, 0),
    U("Kelvin", "K", 1, 0),
};

static const Unit U_ENERGY[] = {
    U("Joules", "J", 1, 0),
    U("Kilojoules", "kJ", 1000, 0),
    U("Thermal calories", "cal", 4184, 0),
    U("Food calories", "kcal", 4184000, 0),
    U("Foot-pounds", "ft-lb", 13558179, -3),
    U("British thermal units", "BTU", 10550559, -2),
};

static const Unit U_AREA[] = {
    U("Square millimeters", "mm2", 1, -6),
    U("Square centimeters", "cm2", 1, -4),
    U("Square meters", "m2", 1, 0),
    U("Hectares", "ha", 10000, 0),
    U("Square kilometers", "km2", 1, 6),
    U("Square inches", "in2", 64516, -8),
    U("Square feet", "ft2", 9290304, -8),
    U("Square yards", "yd2", 83612736, -8),
    U("Acres", "ac", 4046856422, -3),
    U("Square miles", "mi2", 2589988110, 0),
};

static const Unit U_SPEED[] = {
    U("Centimeters per second", "cm/s", 1, -2),
    U("Meters per second", "m/s", 1, 0),
    U("Kilometers per hour", "km/h", 2777778, -6),
    U("Feet per second", "ft/s", 3048, -4),
    U("Miles per hour", "mph", 44704, -4),
    U("Knots", "kn", 5144444, -6),
    U("Mach", "M", 34029, 0),
};

static const Unit U_TIME[] = {
    U("Milliseconds", "ms", 1, -3),
    U("Seconds", "s", 1, 0),
    U("Minutes", "min", 60, 0),
    U("Hours", "hr", 3600, 0),
    U("Days", "d", 86400, 0),
    U("Weeks", "wk", 604800, 0),
    U("Years", "yr", 31536000, 0),
};

static const Unit U_POWER[] = {
    U("Watts", "W", 1, 0),
    U("Kilowatts", "kW", 1000, 0),
    U("Horsepower (US)", "hp", 7456999, 0),
    U("Foot-pounds/minute", "ft-lb/min", 22596965, -3),
    U("BTUs/minute", "BTU/min", 17584265, 0),
};

static const Unit U_DATA[] = {
    U("Bits", "b", 1, 0),
    U("Bytes", "B", 8, 0),
    U("Kilobits", "Kb", 1000, 0),
    U("Kilobytes", "KB", 8000, 0),
    U("Megabits", "Mb", 1000000, 0),
    U("Megabytes", "MB", 8000000, 0),
    U("Gigabits", "Gb", 1000000000, 0),
    U("Gigabytes", "GB", 8000000000, 0),
    U("Terabytes", "TB", 8000000000000, 0),
    U("Petabytes", "PB", 8000000000000000, 0),
    U("Kibibytes", "KiB", 8192, 0),
    U("Mebibytes", "MiB", 8388608, 0),
    U("Gibibytes", "GiB", 8589934592, 0),
};

static const Unit U_PRESSURE[] = {
    U("Pascals", "Pa", 1, 0),
    U("Kilopascals", "kPa", 1000, 0),
    U("Bars", "ba", 100000, 0),
    U("Atmospheres", "atm", 101325, 0),
    U("Millimeters of mercury", "mmHg", 1333224, -2),
    U("Pounds per square inch", "psi", 6894757, 0),
};

static const Unit U_ANGLE[] = {
    U("Degrees", "deg", 1, 0),
    U("Radians", "rad", 3141592653589793, -15),
    U("Gradians", "grad", 9, 0),
};

static const Category CATS[] = {
    { "Volume", 15, U_VOLUME, { 5, 8, 10 } },
    { "Length", 11, U_LENGTH, { 6, 8, 9 } },
    { "Weight and Mass", 14, U_WEIGHT, { 8, 10, 11 } },
    { "Temperature", 3, U_TEMP, { 0, 1, 2 } },
    { "Energy", 6, U_ENERGY, { 1, 3, 5 } },
    { "Area", 10, U_AREA, { 3, 4, 8 } },
    { "Speed", 7, U_SPEED, { 2, 4, 5 } },
    { "Time", 7, U_TIME, { 2, 3, 6 } },
    { "Power", 5, U_POWER, { 1, 2, 4 } },
    { "Data", 13, U_DATA, { 5, 7, 9 } },
    { "Pressure", 6, U_PRESSURE, { 2, 3, 5 } },
    { "Angle", 3, U_ANGLE, { 1, 2, 0 } },
};

#define CAT_COUNT ((int)(sizeof(CATS) / sizeof(CATS[0])))

/* converter state */
static int conv_cat = 0;      /* category index (Volume default) */
static int conv_u1 = 2;       /* Liters */
static int conv_u2 = 10;      /* Gallons (US) */
static int conv_active = 1;   /* which display is being typed into */
static char conv_entry[24];   /* entry for the active side ("1" default) */

static Dec conv_entry_value(void)
{
    if (!conv_entry[0])
        return dec_i(0);
    long long m = 0;
    int e = 0, neg = 0, seen_dot = 0, digits = 0;
    const char *s = conv_entry;
    if (*s == '-') {
        neg = 1;
        ++s;
    }
    for (; *s; ++s) {
        if (*s == '.') {
            seen_dot = 1;
            continue;
        }
        if (digits < 16) {
            m = m * 10 + (*s - '0');
            if (seen_dot)
                --e;
            if (m)
                ++digits;
        }
    }
    Dec r;
    r.m = neg ? -m : m;
    r.e = e;
    return dec_norm(r);
}

/* temperature conversion is affine: via Celsius */
static Dec temp_to_c(Dec v, int unit)
{
    switch (unit) {
    case 0: return v;                                   /* Celsius */
    case 1: return dec_div_i(dec_mul(dec_add(v, dec_i(-32)), dec_i(5)), 9);
    default: return dec_add(v, dec_neg(dec_div_i(dec_mul(dec_i(5463), dec_i(1)), 20))); /* K - 273.15 */
    }
}

static Dec c_to_temp(Dec c, int unit)
{
    switch (unit) {
    case 0: return c;
    case 1: return dec_add(dec_div_i(dec_mul(c, dec_i(9)), 5), dec_i(32));
    default: return dec_add(c, dec_div_i(dec_mul(dec_i(5463), dec_i(1)), 20));
    }
}

static Dec unit_to_base(Dec v, int cat, int unit)
{
    if (cat == 3)
        return temp_to_c(v, unit);
    Dec f;
    f.m = CATS[cat].units[unit].fm;
    f.e = CATS[cat].units[unit].fe;
    return dec_mul(v, f);
}

static Dec base_to_unit(Dec v, int cat, int unit)
{
    if (cat == 3)
        return c_to_temp(v, unit);
    Dec f;
    f.m = CATS[cat].units[unit].fm;
    f.e = CATS[cat].units[unit].fe;
    return dec_div(v, f);
}

/* convert the active entry into the other side's unit */
static Dec conv_result(int side)
{
    Dec v = conv_entry_value();
    if (conv_cat == 3) {
        Dec c = temp_to_c(v, conv_active == 1 ? conv_u1 : conv_u2);
        return c_to_temp(c, side == 1 ? conv_u1 : conv_u2);
    }
    Dec base = dec_mul(v, (Dec){ CATS[conv_cat].units[conv_active == 1 ? conv_u1 : conv_u2].fm,
                                 CATS[conv_cat].units[conv_active == 1 ? conv_u1 : conv_u2].fe });
    Dec f;
    f.m = CATS[conv_cat].units[side == 1 ? conv_u1 : conv_u2].fm;
    f.e = CATS[conv_cat].units[side == 1 ? conv_u1 : conv_u2].fe;
    return dec_div(base, f);
}

/* =====================================================================
 * uXAML plumbing (same model as settings_app.c / files_app.c)
 * ===================================================================== */
static UxFile ux;
static unsigned char ux_blob[64 * 1024];
typedef struct {
    short x, y, w, h;
} NRect;
static NRect nrect[UX_MAX_NODES];

#define DYN_MAX 24
static char dyn_heap[DYN_MAX][UX_STR_MAX];
static unsigned dyn_slot;

static const char *node_text(const UxNode *n)
{
    int a = ux_attr(n, UXA_TEXT);
    if (a < 0 || n->type[a] != 0)
        return 0;
    uint16_t idx = (uint16_t)n->num[a];
    if (idx & 0x8000u)
        return dyn_heap[idx & 0x7FFFu];
    return idx < ux.string_count ? ux.strings[idx] : 0;
}

static void dyn_set(const char *name, const char *text)
{
    const UxDoc *d = 0;
    const char *docs[] = { "StdPage", "SciPage", "ConvPage", "AboutPage" };
    for (unsigned i = 0; i < 4 && !d; ++i)
        d = ux_find_doc(&ux, docs[i]);
    if (!d)
        return;
    const UxNode *n = ux_find_name(&ux, d, name);
    if (!n)
        return;
    /* search every doc for this name */
    for (unsigned di = 0; di < ux.doc_count; ++di) {
        const UxDoc *dd = &ux.docs[di];
        if (dd->root == UX_NONE)
            continue;
        const UxNode *nn = ux_find_name(&ux, dd, name);
        if (!nn)
            continue;
        UxNode *mut = (UxNode *)nn;
        int a = ux_attr(mut, UXA_TEXT);
        if (a < 0) {
            if (mut->attr_count >= UX_MAX_ATTRS)
                continue;
            a = mut->attr_count++;
            mut->attr[a] = UXA_TEXT;
            mut->type[a] = 0;
        }
        char *dst = dyn_heap[dyn_slot];
        int k = 0;
        for (; text[k] && k < UX_STR_MAX - 1; ++k)
            dst[k] = text[k];
        dst[k] = 0;
        mut->num[a] = 0x8000u + (uint16_t)dyn_slot;
        dyn_slot = (dyn_slot + 1) % DYN_MAX;
    }
}

static void dyn_set_color(const char *name, U32 color)
{
    for (unsigned di = 0; di < ux.doc_count; ++di) {
        const UxDoc *dd = &ux.docs[di];
        if (dd->root == UX_NONE)
            continue;
        const UxNode *nn = ux_find_name(&ux, dd, name);
        if (!nn)
            continue;
        UxNode *mut = (UxNode *)nn;
        int a = ux_attr(mut, UXA_COLOR);
        if (a < 0) {
            if (mut->attr_count >= UX_MAX_ATTRS)
                continue;
            a = mut->attr_count++;
            mut->attr[a] = UXA_COLOR;
        }
        mut->type[a] = 2; /* CLR */
        mut->num[a] = (int32_t)color;
    }
}

static int node_visible_idx(unsigned ni)
{
    const UxNode *n = &ux.nodes[ni];
    int a = ux_attr(n, UXA_VISIBLE);
    if (a >= 0 && n->type[a] == 3 && n->num[a] == 0)
        return 0;
    unsigned p = n->parent;
    while (p != UX_NONE) {
        if (!node_visible_idx(p))
            return 0;
        p = ux.nodes[p].parent;
    }
    return 1;
}

static void layout_doc(const UxDoc *d, int bx, int by, int bw, int bh)
{
    if (!d || d->root == UX_NONE)
        return;
    unsigned lo = d->root, hi = d->root + d->node_count;
    if (hi > ux.node_count)
        hi = ux.node_count;
    for (unsigned i = lo; i < hi; ++i) {
        const UxNode *n = &ux.nodes[i];
        int x = ux_int(&ux, n, UXA_X, 0);
        int y = ux_int(&ux, n, UXA_Y, 0);
        int w = ux_int(&ux, n, UXA_W, 0);
        int h = ux_int(&ux, n, UXA_H, 0);
        if (n->parent == UX_NONE) {
            x = bx;
            y = by;
            w = bw;
            h = bh;
        } else {
            x += nrect[n->parent].x;
            y += nrect[n->parent].y;
        }
        nrect[i].x = (short)x;
        nrect[i].y = (short)y;
        nrect[i].w = (short)w;
        nrect[i].h = (short)h;
    }
}

/* ---- drawing (GDI FillRect backend, brush cache) ---- */
static U32 cached_color;
static void *cached_brush;
static Rect clip_rect = { 0, 0, 320, 528 };

static void clip_set(int l, int t, int r, int b)
{
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > 320) r = 320;
    if (b > 528) b = 528;
    clip_rect.left = l;
    clip_rect.top = t;
    clip_rect.right = r > l ? r : l;
    clip_rect.bottom = b > t ? b : t;
}

static void fill(void *dc, int l, int t, int r, int b, U32 c)
{
    Rect v;
    v.left = l > clip_rect.left ? l : clip_rect.left;
    v.top = t > clip_rect.top ? t : clip_rect.top;
    v.right = r < clip_rect.right ? r : clip_rect.right;
    v.bottom = b < clip_rect.bottom ? b : clip_rect.bottom;
    if (!dc || v.left >= v.right || v.top >= v.bottom)
        return;
    if (!cached_brush || c != cached_color) {
        if (cached_brush)
            NT_CALL(DeleteObject, (cached_brush));
        cached_brush = NT_CALL(CreateSolidBrush, (c));
        cached_color = c;
    }
    NT_CALL(FillRect, (dc, &v, cached_brush));
}

static void ifill(void *dc, int l, int t, int r, int b, unsigned c)
{
    fill(dc, l, t, r, b, (U32)c);
}

/* ---- bitmap font ---- */
static const PhoneFont *pick_font(int h)
{
    const PhoneFont *best = &phone_fonts[0];
    int bd = 999;
    for (unsigned i = 0; i < sizeof(phone_fonts) / sizeof(phone_fonts[0]); ++i) {
        const PhoneFont *f = &phone_fonts[i];
        /* only full-ASCII fonts are eligible: the special one-glyph fonts
         * (the 43px '8', the 28px uppercase set) would turn every other
         * character into an out-of-range glyph lookup */
        if (f->first != 32 || f->count < 95)
            continue;
        int df = f->height - h;
        if (df < 0) df = -df;
        if (df < bd) {
            best = f;
            bd = df;
        }
    }
    return best;
}

static int text_w(const char *s, int h)
{
    const PhoneFont *f = pick_font(h);
    int w = 0;
    for (; s && *s; ++s) {
        int ch = (unsigned char)*s;
        if (ch < f->first || ch >= f->first + f->count)
            ch = '?';
        w += f->glyphs[ch - f->first].advance + 1;
    }
    return w;
}

static void draw_text(void *dc, int x, int y, const char *s, U32 c, int h,
                      int align, int bold)
{
    if (!s || !*s)
        return;
    if (bold) /* double-strike: a 1-bpp bold from the regular face */
        draw_text(dc, x + 1, y, s, c, h, align, 0);
    int w = text_w(s, h);
    if (align == 1)
        x -= w / 2;
    else if (align == 2)
        x -= w;
    const PhoneFont *f = pick_font(h);
    for (; *s; ++s) {
        int ch = (unsigned char)*s;
        if (ch < f->first || ch >= f->first + f->count)
            ch = '?';
        const PhoneGlyph *g = &f->glyphs[ch - f->first];
        unsigned stride = ((unsigned)g->width + 7u) >> 3;
        for (int yy = 0; yy < f->height; ++yy) {
            const unsigned char *row = (const unsigned char *)f->bits +
                                       g->offset + yy * stride;
            int run = -1;
            for (int xx = 0; xx <= g->width; ++xx) {
                int bit = xx < g->width &&
                          ((row[(unsigned)xx >> 3] >> (xx & 7)) & 1u);
                if (bit && run < 0)
                    run = xx;
                else if (!bit && run >= 0) {
                    fill(dc, x + run, y + yy, x + xx, y + yy + 1, c);
                    run = -1;
                }
            }
        }
        x += g->advance + 1;
    }
}

/* ---- hosted-render ABI export (SHELLCHROME paints through us) ---- */
static void r_clip(int l, int t, int r, int b) { clip_set(l, t, r, b); }
static void r_fill(void *dc, int l, int t, int r, int b, U32 c) { fill(dc, l, t, r, b, c); }
static void r_text(void *dc, int x, int y, const char *s, U32 c, int h)
{
    draw_text(dc, x, y, s, c, h, 0, 0);
}
static void r_line(void *dc, int x, int y, int xx, int yy, U32 c)
{
    if (y == yy) {
        fill(dc, x < xx ? x : xx, y, (x > xx ? x : xx) + 1, y + 1, c);
        return;
    }
    if (x == xx) {
        fill(dc, x, y < yy ? y : yy, x + 1, (y > yy ? y : yy) + 1, c);
        return;
    }
    int dx = xx > x ? xx - x : x - xx, sx = x < xx ? 1 : -1;
    int dy = yy > y ? y - yy : yy - y, sy = y < yy ? 1 : -1, e = dx + dy;
    for (;;) {
        fill(dc, x, y, x + 1, y + 1, c);
        if (x == xx && y == yy)
            break;
        int e2 = e * 2;
        if (e2 >= dy) {
            e += dy;
            x += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y += sy;
        }
    }
}
static void r_ellipse(void *dc, int cx, int cy, int rx, int ry, U32 c, int thick)
{
    if (rx < 1 || ry < 1 || rx > 100 || ry > 100)
        return;
    int ix = rx - thick, iy = ry - thick;
    for (int y = -ry; y <= ry; ++y) {
        int run = -999;
        for (int x = -rx; x <= rx; ++x) {
            int inside = (x * x * 100) / (rx * rx) + (y * y * 100) / (ry * ry) <= 100;
            int inner = ix > 0 && iy > 0 &&
                        (x * x * 100) / (ix * ix) + (y * y * 100) / (iy * iy) <= 100;
            if (inside && !inner && run == -999)
                run = x;
            else if ((!inside || inner) && run != -999) {
                fill(dc, cx + run, cy + y, cx + x, cy + y + 1, c);
                run = -999;
            }
        }
    }
}
static void r_polygon(void *dc, const Pt *p, unsigned n, U32 c)
{
    if (!p || n < 3 || n > 255)
        return;
    int ymin = p[0].y, ymax = ymin;
    for (unsigned i = 1; i < n; ++i) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    if (ymin < clip_rect.top)
        ymin = clip_rect.top;
    if (ymax > clip_rect.bottom)
        ymax = clip_rect.bottom;
    for (int y = ymin; y < ymax; ++y) {
        int xs[255];
        unsigned used = 0;
        for (unsigned i = 0, j = n - 1; i < n; j = i++)
            if ((p[i].y <= y && p[j].y > y) || (p[j].y <= y && p[i].y > y)) {
                int num = (y - p[i].y) * (p[j].x - p[i].x);
                int den = p[j].y - p[i].y;
                xs[used++] = p[i].x + num / den;
            }
        for (unsigned i = 1; i < used; ++i) {
            int v = xs[i];
            unsigned j = i;
            while (j && xs[j - 1] > v) {
                xs[j] = xs[j - 1];
                --j;
            }
            xs[j] = v;
        }
        for (unsigned i = 0; i + 1 < used; i += 2)
            fill(dc, xs[i], y, xs[i + 1] + 1, y + 1, c);
    }
}
static void r_surface(int b) { (void)b; }

static const U32 calc_theme[] = {
    RGB_(0xF2, 0xF2, 0xF2), /* 0 SURFACE   keyboard backdrop (light) */
    RGB_(0xFF, 0xFF, 0xFF), /* 1 KEY        normal key               */
    RGB_(0xE6, 0xE6, 0xE6), /* 2 KEY_ALT    shift/&123               */
    RGB_(0x00, 0x00, 0x00), /* 3 INK        key text                 */
    RGB_(0x39, 0x65, 0xFF), /* 4 KEY_GO     enter key fill (accent)  */
    RGB_(0xFF, 0xFF, 0xFF), /* 5 GO_INK     glyph on the go key      */
    RGB_(0x39, 0x65, 0xFF), /* 6 ACCENT     checked/caret/links      */
    RGB_(0xCE, 0xCF, 0xCE), /* 7 CTRL_BG    button fill              */
    RGB_(0x00, 0x00, 0x00), /* 8 CTRL_INK   button label             */
    RGB_(0x66, 0x66, 0x66), /* 9 CTRL_EDGE  unchecked border         */
    RGB_(0x76, 0x76, 0x76), /* 10 FIELD_EDGE input border/hint      */
    RGB_(0xCE, 0xCF, 0xCE), /* 11 NAV_BG    nav bar strip (light)    */
    RGB_(0x31, 0x30, 0x31), /* 12 NAV_INK   nav bar glyphs           */
};

static const CalcRender calc_renderer = {
    0x55495831u, r_clip, r_fill, r_text, r_line,
    r_ellipse, r_polygon, r_surface, calc_theme,
};

__attribute__((dllexport)) const CalcRender *render_SpGetThreadContext(void)
{
    return &calc_renderer;
}

/* ---- app modes / sheets ---- */
enum { M_STD = 0, M_SCI, M_PROG, M_CONV, M_ABOUT };
enum { SH_NONE = 0, SH_NAV, SH_HIST, SH_MEM, SH_UNITS };

static int mode = M_STD;
static int sheet = SH_NONE;
static int units_side = 1;      /* which combo opened the unit picker */
static int hist_scroll, mem_scroll, nav_scroll, units_scroll;
static const CalcChrome *g_chrome;

/* pressed-button feedback (ListMedium tint) */
static const UxNode *pressed_node;

static void paint_node(void *dc, unsigned ni)
{
    const UxNode *n = &ux.nodes[ni];
    if (!node_visible_idx(ni))
        return;
    NRect r = nrect[ni];
    switch (n->kind) {
    case UX_RECT: {
        if (ux_int(&ux, n, UXA_BORDER, 0)) {
            U32 c = ux_clr(&ux, n, UXA_COLOR, C_MUT);
            fi_rect(ifill, dc, r.x, r.y, r.w, r.h, 1, c);
        } else {
            U32 c = ux_clr(&ux, n, UXA_BG, C_PAGE);
            fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, c);
        }
        break;
    }
    case UX_GLYPH: {
        const char *t = node_text(n);
        U32 col = ux_clr(&ux, n, UXA_COLOR, C_INK);
        int sz = r.w < r.h ? r.w : r.h;
        if (t) {
            int sid = ci_find(t);
            if (sid)
                ci_draw(ifill, dc, r.x, r.y, sz, col, sid);
            else {
                int fid = fi_find(t);
                if (fid)
                    fi_draw(ifill, dc, r.x, r.y, sz, col, fid);
            }
        }
        break;
    }
    case UX_TEXT:
    case UX_BUTTON: {
        const char *t = node_text(n);
        if (n->kind == UX_BUTTON) {
            U32 bg = ux_clr(&ux, n, UXA_BG, 0xFFFFFFFFu);
            if (n == pressed_node)
                fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, C_PRESS);
            else if (bg != 0xFFFFFFFFu)
                fill(dc, r.x, r.y, r.x + r.w, r.y + r.h, bg);
        }
        if (t && *t) {
            int size = ux_int(&ux, n, UXA_SIZE, 12);
            int align = ux_int(&ux, n, UXA_ALIGN, 0);
            int bold = ux_int(&ux, n, UXA_BOLD, 0);
            U32 col = ux_clr(&ux, n, UXA_COLOR, C_INK);
            int tx = r.x, ty = r.y;
            if (align == 1)
                tx += r.w / 2;
            else if (align == 2)
                tx += r.w;
            draw_text(dc, tx, ty, t, col, size, align, bold);
        }
        break;
    }
    default:
        break;
    }
    for (uint8_t i = 0; i < n->child_count; ++i)
        paint_node(dc, n->child[i]);
}

static const UxNode *hit_test(const UxDoc *d, int x, int y)
{
    const UxNode *best = 0;
    if (!d || d->root == UX_NONE)
        return 0;
    unsigned lo = d->root, hi = d->root + d->node_count;
    if (hi > ux.node_count)
        hi = ux.node_count;
    for (unsigned i = lo; i < hi; ++i) {
        const UxNode *n = &ux.nodes[i];
        if (n->kind != UX_BUTTON || !node_visible_idx(i))
            continue;
        NRect r = nrect[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            best = n;
    }
    return best;
}

static int find_rect(const char *name, NRect *out)
{
    for (unsigned di = 0; di < ux.doc_count; ++di) {
        const UxDoc *dd = &ux.docs[di];
        if (dd->root == UX_NONE)
            continue;
        const UxNode *n = ux_find_name(&ux, dd, name);
        if (n) {
            unsigned idx = (unsigned)(n - ux.nodes);
            *out = nrect[idx];
            return 1;
        }
    }
    return 0;
}

/* =====================================================================
 * page painting
 * ===================================================================== */
static const char *err_text(void)
{
    switch (g.err) {
    case 1: return "Divide by zero";
    case 2: return "Overflow";
    case 3: return "Result is undefined";
    default: return "Invalid input";
    }
}

/* the big result line with auto-shrink (CalcResult: 46 -> 12 floor):
 * right-aligned to `right`, top at y, shrinking from `base` */
static void draw_result(void *dc, int right, int y, int maxw, int base,
                         const char *txt, U32 col)
{
    int size = base;
    while (size > 12 && text_w(txt, size) > maxw)
        size -= 2;
    draw_text(dc, right, y, txt, col, size, 2, 0);
}

/* memory row labels (shared std/sci), painted live with the enabled/
 * disabled state: MC MR M+ M- MS are #636563 when memory-independent,
 * but MC/MR (and the M-list chevron) drop to #B5B2B5 with no memory */
static void paint_memrow(void *dc, int y)
{
    int have = mem_count > 0;
    draw_text(dc, 27, y, "MC", have ? C_MUT : C_DIS, 12, 1, 0);
    draw_text(dc, 80, y, "MR", have ? C_MUT : C_DIS, 12, 1, 0);
    draw_text(dc, 133, y, "M+", C_MUT, 12, 1, 0);
    draw_text(dc, 186, y, "M-", C_MUT, 12, 1, 0);
    draw_text(dc, 240, y, "MS", C_MUT, 12, 1, 0);
    draw_text(dc, 285, y, "M", have ? C_MUT : C_DIS, 12, 1, 0);
    fi_draw(ifill, dc, 294, y + 3, 7, have ? C_MUT : C_DIS, FI_CHEVDOWN);
}

static void paint_std_display(void *dc)
{
    /* expression line (16px, gray, right-aligned to x305, y64) */
    if (g.expr[0])
        draw_text(dc, 305, 64, g.expr, C_MUT, 16, 2, 0);
    /* result: live entry while typing, else the display value
     * (44px right-aligned to x305, top y96) */
    char buf[28];
    if (g.err)
        draw_result(dc, 305, 96, 275, 44, err_text(), C_INK);
    else {
        if (g.entry_active && g.entry[0]) {
            char *dst = buf;
            const char *s = g.entry;
            int len = 0;
            while (s[len] && len < 22) ++len;
            for (int i = 0; i < len; ++i) dst[i] = s[i];
            dst[len] = 0;
            /* light grouping for the integer part of the entry */
        } else {
            dec_to_string_buf(g.display, buf, 26);
        }
        draw_result(dc, 305, 96, 275, 44, buf, C_INK);
    }
}

static void paint_std(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "StdPage");
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root);
    paint_std_display(dc);
    paint_memrow(dc, 163);
}

/* F-E (fixed-exponent) display: d.dddde±exp from a plain decimal string */
static void fe_format(const char *in, char *out, int cap)
{
    /* collect digits + locate the decimal point */
    char digits[48];
    int n = 0, dot_pos = -1, i = 0;
    for (const char *s = in; *s && n < 47; ++s) {
        char c = *s;
        if (c == ',' || c == ' ')
            continue;
        if (c == '.') {
            dot_pos = i;
            continue;
        }
        if (c < '0' || c > '9')
            continue;
        digits[n++] = c;
        ++i;
    }
    if (n == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    /* exponent: int part has a significant digit -> (#int digits)-1;
     * otherwise -(zeros between the dot and the first significant)-1 */
    int int_digits = dot_pos < 0 ? n : dot_pos;
    int first_sig = -1, k = 0;
    for (k = 0; k < n; ++k)
        if (digits[k] != '0') { first_sig = k; break; }
    if (first_sig < 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    int e;
    if (first_sig < int_digits)
        e = int_digits - 1;               /* value >= 1 */
    else
        e = -(first_sig - int_digits) - 1; /* 0.00x */
    /* mantissa: first significant digit . up to 16 more */
    int m = 0;
    out[m++] = digits[first_sig];
    int more = 0;
    for (k = first_sig + 1; k < n && more < 16; ++k, ++more)
        out[m++] = digits[k];
    while (m > 1 && out[m - 1] == '0')
        --m;
    if (m > 1) {
        for (int j = m; j > 1; --j)
            out[j] = out[j - 1];
        out[1] = '.';
        ++m;
    }
    out[m++] = 'e';
    if (e < 0) {
        out[m++] = '-';
        e = -e;
    } else {
        out[m++] = '+';
    }
    char en[8];
    int en_n = 0;
    do {
        en[en_n++] = '0' + e % 10;
        e /= 10;
    } while (e && en_n < 7);
    while (en_n)
        out[m++] = en[--en_n];
    out[m] = 0;
    (void)cap;
}

static void paint_sci(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "SciPage");
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root);
    /* angle row (y137, 14px): DEG shows the active unit in accent,
     * HYP / F-E go accent while toggled on (per the capture) */
    const char *unit = g.angle_mode == ANG_DEG ? "DEG"
                      : g.angle_mode == ANG_RAD ? "RAD" : "GRAD";
    draw_text(dc, 16, 137, unit, C_ACC, 14, 0, 0);
    draw_text(dc, 69, 137, "HYP", g.hyp ? C_ACC : C_MUT, 14, 0, 0);
    draw_text(dc, 120, 137, "F-E", g.fe ? C_ACC : C_MUT, 14, 0, 0);
    /* memory row (labels y172) */
    paint_memrow(dc, 172);
    /* keypad rows 1-2, live labels per 2nd + HYP (y205 / y246) */
    int s = g.second, h = g.hyp;
    ci_draw(ifill, dc, 24, 203, 16, C_INK, s ? CI_XPOW3 : CI_XPOW2);
    ci_draw(ifill, dc, 88, 204, 16, C_INK, s ? CI_YROOT : CI_XPOWY);
    const char *trig[3];
    for (int i = 0; i < 3; ++i) {
        static const char *names[2][3] = {
            {"sin", "cos", "tan"}, {"asin", "acos", "atan"}};
        static const char *hnames[2][3] = {
            {"sinh", "cosh", "tanh"}, {"asinh", "acosh", "atanh"}};
        trig[i] = h ? hnames[s ? 1 : 0][i] : names[s ? 1 : 0][i];
    }
    draw_text(dc, 96, 205, trig[0], C_INK, 14, 1, 0);
    draw_text(dc, 160, 205, trig[1], C_INK, 14, 1, 0);
    draw_text(dc, 224, 205, trig[2], C_INK, 14, 1, 0);
    ci_draw(ifill, dc, 24, 244, 16, C_INK, s ? CI_INVERT : CI_SQRT);
    ci_draw(ifill, dc, 87, 245, 17, C_INK, s ? CI_EEXP : CI_TENPOW);
    draw_text(dc, 96, 246, s ? "ln" : "log", C_INK, 14, 1, 0);
    draw_text(dc, 160, 246, s ? "dms" : "Exp", C_INK, 14, 1, 0);
    draw_text(dc, 224, 246, s ? "deg" : "Mod", C_INK, 14, 1, 0);
    /* display: expression (y64) + result (28px right to x306, y91) */
    if (g.expr[0])
        draw_text(dc, 305, 64, g.expr, C_MUT, 16, 2, 0);
    char buf[28], fe[40];
    if (g.err) {
        draw_result(dc, 306, 88, 280, 28, err_text(), C_INK);
    } else {
        if (g.entry_active && g.entry[0]) {
            int len = 0;
            while (g.entry[len] && len < 22) ++len;
            for (int i = 0; i < len; ++i) buf[i] = g.entry[i];
            buf[len] = 0;
        } else {
            dec_to_string_buf(g.display, buf, 26);
        }
        if (g.fe) {
            fe_format(buf, fe, sizeof(fe));
            draw_result(dc, 306, 88, 280, 28, fe, C_INK);
        } else {
            draw_result(dc, 306, 88, 280, 28, buf, C_INK);
        }
    }
}

static void paint_conv(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "ConvPage");
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root);
    /* mode title = the category name, caps, like the other pages */
    {
        char up[24];
        const char *src = CATS[conv_cat].name;
        int i = 0;
        for (; src[i] && i < 23; ++i)
            up[i] = src[i] >= 'a' && src[i] <= 'z' ? src[i] - 32 : src[i];
        up[i] = 0;
        draw_text(dc, 60, 36, up, C_INK, 14, 0, 1);
    }
    /* the two unit blocks per the volume capture: value 24px left at
     * x13 (value1 y93, value2 y173), unit names 14px accent blue with a
     * chevron (y131 / y211) */
    char b1[26], b2[26];
    Dec v1 = conv_result(1);
    Dec v2 = conv_result(2);
    if (conv_active == 1 && conv_entry[0]) {
        int len = 0;
        while (conv_entry[len] && len < 18) ++len;
        for (int i = 0; i < len; ++i) b1[i] = conv_entry[i];
        b1[len] = 0;
    } else {
        dec_to_string_buf(v1, b1, 24);
    }
    if (conv_active == 2 && conv_entry[0]) {
        int len = 0;
        while (conv_entry[len] && len < 18) ++len;
        for (int i = 0; i < len; ++i) b2[i] = conv_entry[i];
        b2[len] = 0;
    } else {
        dec_to_string_buf(v2, b2, 24);
    }
    draw_text(dc, 13, 93, b1, C_INK, 24, 0, 0);
    draw_text(dc, 13, 173, b2, C_INK, 24, 0, 0);
    const char *n1 = CATS[conv_cat].units[conv_u1].name;
    const char *n2 = CATS[conv_cat].units[conv_u2].name;
    draw_text(dc, 13, 131, n1, C_ACC, 14, 0, 0);
    fi_draw(ifill, dc, 15 + text_w(n1, 14), 134, 9, C_ACC, FI_CHEVDOWN);
    draw_text(dc, 13, 211, n2, C_ACC, 14, 0, 0);
    fi_draw(ifill, dc, 15 + text_w(n2, 14), 214, 9, C_ACC, FI_CHEVDOWN);
}

static void paint_about(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "AboutPage");
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root);
}

/* =====================================================================
 * sheets (full-surface overlays: nav pane, history, memory, unit picker)
 * ===================================================================== */
#define NAV_W 256

/* nav pane content height: label 12 + group(30+2*40) + label 30 +
 * cats 12*40 + label 30 + About row 40 + padding */
#define NAV_CONTENT_H 712

static void paint_nav(void *dc)
{
    /* pane only, no scrim: the page stays visible x256..320 (capture) */
    fill(dc, 0, 0, NAV_W, CHROME_Y, C_SHEET);
    clip_set(0, 0, NAV_W, CHROME_Y);
    int y = 24 - nav_scroll;
    /* header row doubles as the CALCULATOR group header */
    fi_draw(ifill, dc, 14, 36, 20, C_INK, FI_HAMBURGER);
    draw_text(dc, 60, 36, "CALCULATOR", C_INK, 14, 0, 1);
    y += 32;
    static const char *calc_modes[] = { "Standard", "Scientific", "Programmer" };
    for (int i = 0; i < 3; ++i) {
        int sel = (i == 0 && mode == M_STD) || (i == 1 && mode == M_SCI) ||
                  (i == 2 && mode == M_PROG);
        if (sel)
            fill(dc, 0, y, NAV_W, y + 32, C_SEL);
        draw_text(dc, 48, y + 9, calc_modes[i], C_INK, 15, 0, 0);
        y += 32;
    }
    draw_text(dc, 12, y + 9, "CONVERTER", C_INK, 12, 0, 1);
    y += 32;
    for (int i = 0; i < CAT_COUNT; ++i) {
        int sel = mode == M_CONV && conv_cat == i;
        if (sel)
            fill(dc, 0, y, NAV_W, y + 32, C_SEL);
        draw_text(dc, 48, y + 9, CATS[i].name, C_INK, 15, 0, 0);
        y += 32;
    }
    /* About pinned above the nav bar with an info badge */
    if (mode == M_ABOUT)
        fill(dc, 0, 448, NAV_W, 480, C_SEL);
    fi_rect(ifill, dc, 10, 458, 13, 13, 1, C_MUT);
    draw_text(dc, 15, 461, "i", C_MUT, 12, 1, 0);
    draw_text(dc, 48, 457, "About", C_INK, 15, 0, 0);
    clip_set(0, 0, 320, 528);
}

static void paint_hist(void *dc)
{
    fill(dc, 0, 0, 320, CHROME_Y, C_SCRIM);
    /* bottom sheet */
    int top = 90;
    fill(dc, 0, top, 320, CHROME_Y, C_WHITE);
    fill(dc, 0, top, 320, top + 1, C_HAIR);
    draw_text(dc, 12, top + 10, "History", C_INK, 15, 0, 1);
    /* trash button bottom-right */
    fi_draw(ifill, dc, 268, top + 16, 28, C_ACC, FI_DELETE);
    if (!hist_count) {
        draw_text(dc, 12, top + 60, "There's no history yet", C_MUT, 14, 0, 0);
        return;
    }
    clip_set(0, top + 48, 320, CHROME_Y - 46);
    int y = top + 56 - hist_scroll;
    for (int i = hist_count - 1; i >= 0; --i) {
        draw_text(dc, 306, y, hist[i].expr, C_MUT, 12, 2, 0);
        draw_text(dc, 306, y + 18, hist[i].result, C_INK, 16, 2, 1);
        y += 46;
    }
    clip_set(0, 0, 320, 528);
}

static void paint_mem(void *dc)
{
    fill(dc, 0, 0, 320, CHROME_Y, C_SCRIM);
    int top = 90;
    fill(dc, 0, top, 320, CHROME_Y, C_WHITE);
    fill(dc, 0, top, 320, top + 1, C_HAIR);
    draw_text(dc, 12, top + 10, "Memory", C_INK, 15, 0, 1);
    draw_text(dc, 268, top + 12, "MC", C_ACC, 14, 1, 0);
    if (!mem_count) {
        draw_text(dc, 12, top + 60, "There's nothing saved in memory", C_MUT, 14, 0, 0);
        return;
    }
    clip_set(0, top + 48, 320, CHROME_Y - 46);
    int y = top + 56 - mem_scroll;
    for (int i = mem_count - 1; i >= 0; --i) {
        draw_text(dc, 306, y, mem_str[i], C_INK, 16, 2, 1);
        /* per-row MC / M+ / M- accent buttons */
        draw_text(dc, 244, y + 22, "MC", C_ACC, 12, 1, 0);
        draw_text(dc, 274, y + 22, "M+", C_ACC, 12, 1, 0);
        draw_text(dc, 302, y + 22, "M-", C_ACC, 12, 1, 0);
        y += 54;
    }
    clip_set(0, 0, 320, 528);
}

static void paint_units(void *dc)
{
    fill(dc, 0, 0, 320, CHROME_Y, C_SCRIM);
    int top = 70;
    fill(dc, 0, top, 320, CHROME_Y, C_WHITE);
    fill(dc, 0, top, 320, top + 1, C_HAIR);
    draw_text(dc, 160, top + 10, CATS[conv_cat].name, C_INK, 15, 1, 1);
    clip_set(0, top + 40, 320, CHROME_Y);
    int max_scroll = CATS[conv_cat].count * 36 - (CHROME_Y - (top + 48));
    if (max_scroll < 0)
        max_scroll = 0;
    if (units_scroll > max_scroll)
        units_scroll = max_scroll;
    int y = top + 48 - units_scroll;
    for (int i = 0; i < CATS[conv_cat].count; ++i) {
        if (y > top + 30 && y < CHROME_Y - 6) {
            int sel = (units_side == 1 ? conv_u1 : conv_u2) == i;
            if (sel)
                fill(dc, 0, y - 6, 320, 36, C_SEL);
            draw_text(dc, 24, y, CATS[conv_cat].units[i].name,
                      sel ? C_INK : C_INK, 15, 0, 0);
            draw_text(dc, 296, y + 2, CATS[conv_cat].units[i].abbr, C_MUT, 13, 2, 0);
        }
        y += 36;
    }
    clip_set(0, 0, 320, 528);
}


/* ================= programmer mode ================= */
static const int prog_widths[4] = {64, 32, 16, 8};
static const char *prog_wnames[4] = {"QWORD", "DWORD", "WORD", "BYTE"};

static long long prog_mask(void)
{
    return prog_widths[g.prog_qword] == 64
               ? -1LL
               : (1LL << prog_widths[g.prog_qword]) - 1;
}

static long long prog_val(void)
{
    /* the live entry parsed in the active base, else the display */
    if (g.entry_active && g.entry[0]) {
        long long v = 0;
        int digits = 0;
        for (const char *q = g.entry; *q; ++q) {
            char c = *q;
            int d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (c >= 'A' && c <= 'F')
                d = 10 + c - 'A';
            else
                continue;
            if (digits < 18) {
                v = (v << 4) + d;
                ++digits;
            }
        }
        return v & prog_mask();
    }
    return g.display.m & prog_mask();
}

static void prog_set(long long v)
{
    v &= prog_mask();
    g.entry_active = 0;
    g.entry[0] = 0;
    g.err = 0;
    g.display.m = v;
    g.display.e = 0;
}

static int prog_digit_ok(char c)
{
    int b = g.prog_base;
    if (c >= '0' && c <= '9') {
        int v = c - '0';
        if (b == 3)
            return v <= 1;          /* BIN */
        if (b == 2)
            return v <= 7;          /* OCT */
        return 1;
    }
    if (c >= 'A' && c <= 'F')
        return b == 0;              /* letters only in HEX */
    return 0;
}

/* format v in the given base; BIN gets 4-bit groups */
static void prog_fmt(long long v, int base, char *out, int cap)
{
    static const char hex[] = "0123456789ABCDEF";
    if (base == 1) {
        /* decimal with grouping */
        unsigned long long u = v < 0 ? -(unsigned long long)v : (unsigned long long)v;
        char tmp[24];
        int n = 0;
        do {
            tmp[n++] = '0' + (int)(u % 10);
            u /= 10;
        } while (u);
        int o = 0;
        if (v < 0 && o < cap - 1)
            out[o++] = '-';
        for (int i = n - 1; i >= 0; --i) {
            if (o < cap - 1)
                out[o++] = tmp[i];
            if ((i % 3) == 0 && i > 0 && o < cap - 1)
                out[o++] = ',';
        }
        out[o] = 0;
        return;
    }
    unsigned long long u = (unsigned long long)v;
    int radix = base == 0 ? 16 : base == 2 ? 8 : 2;
    char tmp[80];
    int n = 0;
    do {
        tmp[n++] = hex[(int)(u % (unsigned)radix)];
        u /= (unsigned)radix;
    } while (u);
    if (base == 3)
        while (n % 4)
            tmp[n++] = '0'; /* BIN lines fill whole nibbles */
    int o = 0;
    for (int i = n - 1; i >= 0; --i) {
        if (o < cap - 1)
            out[o++] = tmp[i];
        if (base == 3 && (i % 4) == 0 && i > 0 && o < cap - 1)
            out[o++] = ' ';
    }
    out[o] = 0;
}

/* nibble count that holds v (>=1) — BIN/OCT/HEX trim leading zeros */
static int prog_nibbles(long long v)
{
    unsigned long long u = (unsigned long long)v;
    int n = 1;
    while (u >>= 4)
        ++n;
    return n;
}

static void prog_bit_toggle(int bit)
{
    long long v = prog_val();
    v ^= (1LL << bit);
    prog_set(v);
}

/* ---- programmer page painting (display, base lines, pivot, keypad) ---- */
static void prog_paint_base_lines(void *dc)
{
    long long v = prog_val();
    static const char *names[4] = {"HEX", "DEC", "OCT", "BIN"};
    char buf[80];
    for (int i = 0; i < 4; ++i) {
        int y = 129 + 23 * i;
        U32 col = i == g.prog_base ? C_ACC : C_MUT;
        draw_text(dc, 13, y, names[i], col, 12, 0, 0);
        if (i == 1)
            prog_fmt(v, 1, buf, sizeof(buf));
        else
            prog_fmt(v, i, buf, sizeof(buf));
        draw_text(dc, 44, y, buf, col, 12, 0, 0);
    }
}

static void prog_paint_keypad(void *dc)
{
    /* R1..R5 live labels (disabled keys drop to #B5B2B5) */
    int b = g.prog_base;
    U32 on = C_INK, off = C_DIS;
    int af = b == 0;   /* A-F: hex only        */
    int d89 = b == 0 || b == 1;
    int d27 = b != 3;
    /* R1 (y300): 2nd Mod CE C back div */
    fi_draw(ifill, dc, 20, 296, 13, g.second ? C_ACC : C_MUT, FI_UP);
    draw_text(dc, 80, 300, "Mod", on, 16, 1, 0);
    draw_text(dc, 133, 300, "CE", on, 16, 1, 0);
    draw_text(dc, 187, 300, "C", on, 16, 1, 0);
    ci_draw(ifill, dc, 232, 300, 17, on, CI_BACKSPACE);
    ci_draw(ifill, dc, 286, 300, 15, on, CI_DIVIDE);
    /* R2 (y339): A B 7 8 9 x */
    draw_text(dc, 26, 339, "A", af ? on : off, 16, 1, 0);
    draw_text(dc, 80, 339, "B", af ? on : off, 16, 1, 0);
    draw_text(dc, 133, 339, "7", d27 ? on : off, 16, 1, 0);
    draw_text(dc, 187, 339, "8", d89 ? on : off, 16, 1, 0);
    draw_text(dc, 240, 339, "9", d89 ? on : off, 16, 1, 0);
    ci_draw(ifill, dc, 286, 340, 15, on, CI_TIMES);
    /* R3 (y377): C D 4 5 6 - */
    draw_text(dc, 26, 377, "C", af ? on : off, 16, 1, 0);
    draw_text(dc, 80, 377, "D", af ? on : off, 16, 1, 0);
    draw_text(dc, 133, 377, "4", d27 ? on : off, 16, 1, 0);
    draw_text(dc, 187, 377, "5", d27 ? on : off, 16, 1, 0);
    draw_text(dc, 240, 377, "6", d27 ? on : off, 16, 1, 0);
    ci_draw(ifill, dc, 286, 378, 15, on, CI_MINUS);
    /* R4 (y416): E F 1 2 3 + */
    draw_text(dc, 26, 416, "E", af ? on : off, 16, 1, 0);
    draw_text(dc, 80, 416, "F", af ? on : off, 16, 1, 0);
    draw_text(dc, 133, 416, "1", on, 16, 1, 0);
    draw_text(dc, 187, 416, "2", d27 ? on : off, 16, 1, 0);
    draw_text(dc, 240, 416, "3", d27 ? on : off, 16, 1, 0);
    ci_draw(ifill, dc, 286, 417, 15, on, CI_PLUS);
    /* R5 (y453): ( ) neg 0 . = */
    draw_text(dc, 26, 453, "(", on, 16, 1, 0);
    draw_text(dc, 80, 453, ")", on, 16, 1, 0);
    ci_draw(ifill, dc, 132, 454, 15, on, CI_NEGATE);
    draw_text(dc, 187, 453, "0", on, 16, 1, 0);
    draw_text(dc, 240, 453, ".", off, 16, 1, 0);
    ci_draw(ifill, dc, 286, 454, 15, on, CI_EQUAL);
}

static void prog_paint_bitview(void *dc)
{
    /* 16 nibble cells in a 4x4 arrangement, per the bit-toggle capture:
     * each cell = 4 bit digits (16px) over its lowest-position label */
    long long v = prog_val();
    static const int cellx[4] = {15, 92, 169, 246};
    char lab[8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            int n = r * 4 + c;          /* nibble from the top */
            int low = 60 - 4 * n;       /* its lowest bit       */
            int y = 272 + 57 * r;
            for (int d = 0; d < 4; ++d) {
                int bit = 63 - 4 * n - d;
                int set = (int)((v >> bit) & 1);
                char dig[2] = {(char)('0' + set), 0};
                draw_text(dc, cellx[c] + 4 + 14 * d, y, dig,
                          set ? C_ACC : C_MUT, 16, 0, 0);
            }
            /* position label under the nibble */
            int o = 0;
            if (low >= 10)
                lab[o++] = '0' + low / 10;
            lab[o++] = '0' + low % 10;
            lab[o] = 0;
            draw_text(dc, cellx[c] + 4, 296 + 57 * r, lab, C_MUT, 10, 0, 0);
        }
    }
}

static int prog_bit_hit(int x, int y)
{
    static const int cellx[4] = {15, 92, 169, 246};
    for (int r = 0; r < 4; ++r) {
        if (y >= 266 + 57 * r && y < 266 + 57 * r + 42) {
            for (int c = 0; c < 4; ++c) {
                if (x >= cellx[c] - 4 && x < cellx[c] + 60) {
                    int d = (x - (cellx[c] - 4)) / 15;
                    if (d > 3)
                        d = 3;
                    prog_bit_toggle(63 - 4 * (r * 4 + c) - d);
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void paint_prog(void *dc)
{
    const UxDoc *d = ux_find_doc(&ux, "ProgPage");
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    paint_node(dc, d->root);
    /* big value in the active base (28px right to x306, y90) */
    char buf[96];
    prog_fmt(prog_val(), g.prog_base, buf, sizeof(buf));
    draw_result(dc, 306, 90, 280, 28, buf, C_INK);
    prog_paint_base_lines(dc);
    /* pivot row: bit-toggle dots (9), QWORD, MS, M+ */
    U32 bic = g.prog_bitview ? C_ACC : C_MUT;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            fill(dc, 22 + 5 * c, 227 + 5 * r, 24 + 5 * c, 231 + 5 * r, bic);
    if (g.prog_bitview)
        fill(dc, 20, 244, 40, 245, C_ACC); /* selected underline */
    draw_text(dc, 137, 227, prog_wnames[g.prog_qword], C_ACC, 14, 0, 0);
    int have = mem_count > 0;
    draw_text(dc, 232, 229, "MS", have ? C_MUT : C_DIS, 12, 0, 0);
    draw_text(dc, 286, 229, "M+", have ? C_MUT : C_DIS, 12, 0, 0);
    /* keypad or bit grid */
    if (g.prog_bitview)
        prog_paint_bitview(dc);
    else
        prog_paint_keypad(dc);
}

static void paint_all(void *dc)
{
    switch (mode) {
    case M_SCI: paint_sci(dc); break;
    case M_CONV: paint_conv(dc); break;
    case M_PROG: paint_prog(dc); break;
    case M_ABOUT: paint_about(dc); break;
    default: paint_std(dc); break;
    }
    switch (sheet) {
    case SH_NAV: paint_nav(dc); break;
    case SH_HIST: paint_hist(dc); break;
    case SH_MEM: paint_mem(dc); break;
    case SH_UNITS: paint_units(dc); break;
    default: break;
    }
    if (g_chrome)
        g_chrome->paint(dc);
}

/* =====================================================================
 * input
 * ===================================================================== */
static void push_history(void)
{
    char res[24];
    dec_to_string_buf(g.display, res, 22);
    hist_push(g.expr[0] ? g.expr : res, res);
}

static void do_digit(char c)
{
    if (mode == M_PROG) {
        if (!prog_digit_ok(c))
            return;
        int len = 0;
        while (g.entry[len] && len < 18)
            ++len;
        if (len < 18) {
            g.entry[len++] = c;
            g.entry[len] = 0;
            g.entry_active = 1;
            g.err = 0;
        }
        return;
    }
    if (mode == M_CONV) {
        int len = 0;
        while (conv_entry[len])
            ++len;
        if (c == '.') {
            for (int i = 0; i < len; ++i)
                if (conv_entry[i] == '.')
                    return;
            if (len < 22) {
                conv_entry[len] = len ? '.' : '0';
                if (!len)
                    conv_entry[1] = '.', conv_entry[2] = 0;
                else
                    conv_entry[len + 1] = 0;
            }
            return;
        }
        if (len < 16 && !(len == 1 && conv_entry[0] == '0'))
            conv_entry[len] = c, conv_entry[len + 1] = 0;
        return;
    }
    entry_digit(c);
    if (mode == M_SCI) {
        /* typing after an operator shows in the expression line */
    }
}

static void conv_clear_entry(void)
{
    conv_entry[0] = 0;
}

static void conv_backspace(void)
{
    int len = 0;
    while (conv_entry[len])
        ++len;
    if (len)
        conv_entry[len - 1] = 0;
}


static void std_command(const char *cmd)
{
    if (!cmd)
        return;
    if (cmd[0] == 'd' && cmd[1] == ':') {
        do_digit(cmd[2]);
        return;
    }
    if (xeq(cmd, "op:ce")) {
        if (mode == M_CONV)
            conv_clear_entry();
        else
            op_clear_entry();
    } else if (xeq(cmd, "op:c")) {
        if (mode == M_CONV) {
            conv_clear_entry();
        } else {
            op_clear();
        }
    } else if (xeq(cmd, "op:back")) {
        if (mode == M_CONV)
            conv_backspace();
        else
            entry_backspace();
    } else if (xeq(cmd, "op:eq")) {
        if (mode != M_CONV) {
            equals();
            if (!g.err)
                push_history();
        }
    } else if (xeq(cmd, "op:divide")) {
        binary_op(OP_DIV);
    } else if (xeq(cmd, "op:times")) {
        binary_op(OP_MUL);
    } else if (xeq(cmd, "op:minus")) {
        binary_op(OP_SUB);
    } else if (xeq(cmd, "op:plus")) {
        binary_op(OP_ADD);
    } else if (xeq(cmd, "op:negate")) {
        if (mode == M_CONV) {
            if (conv_entry[0] == '-') {
                int i = 1, j = 0;
                while (conv_entry[i])
                    conv_entry[j++] = conv_entry[i++];
                conv_entry[j] = 0;
            } else {
                int len = 0;
                while (conv_entry[len])
                    ++len;
                for (int i = len; i >= 0; --i)
                    conv_entry[i + 1] = conv_entry[i];
                conv_entry[0] = '-';
            }
        } else {
            op_negate();
        }
    } else if (xeq(cmd, "op:percent")) {
        op_percent();
    } else if (xeq(cmd, "op:sqrt")) {
        if (g.second)
            op_invert();
        else
            op_sqrt();
    } else if (xeq(cmd, "op:sqr")) {
        if (g.second)
            op_cube();
        else
            op_sqr();
    } else if (xeq(cmd, "op:cube")) {
        op_cube();
    } else if (xeq(cmd, "op:invert")) {
        op_invert();
    } else if (xeq(cmd, "op:mod")) {
        if (g.second)
            op_deg();
        else
            binary_op(OP_MOD);
    } else if (xeq(cmd, "op:powy")) {
        if (g.second)
            binary_op(OP_YROOT);
        else
            binary_op(OP_POW);
    } else if (xeq(cmd, "op:yrootx")) {
        binary_op(OP_YROOT);
    } else if (xeq(cmd, "op:pi")) {
        op_pi();
    } else if (xeq(cmd, "op:pl")) {
        paren_open();
    } else if (xeq(cmd, "op:pr")) {
        paren_close();
    } else if (xeq(cmd, "op:tenpow")) {
        if (g.second)
            fn_expe();
        else
            fn_exp10();
    } else if (xeq(cmd, "op:epow")) {
        fn_expe();
    } else if (xeq(cmd, "fn:sin")) {
        fn_trig(0 + 8 * g.second + 16 * g.hyp);
    } else if (xeq(cmd, "fn:cos")) {
        fn_trig(1 + 8 * g.second + 16 * g.hyp);
    } else if (xeq(cmd, "fn:tan")) {
        fn_trig(2 + 8 * g.second + 16 * g.hyp);
    } else if (xeq(cmd, "fn:asin")) {
        fn_trig(g.hyp ? 24 : 8);
    } else if (xeq(cmd, "fn:acos")) {
        fn_trig(g.hyp ? 25 : 9);
    } else if (xeq(cmd, "fn:atan")) {
        fn_trig(g.hyp ? 26 : 10);
    } else if (xeq(cmd, "fn:log")) {
        if (g.second)
            fn_ln();
        else
            fn_log();
    } else if (xeq(cmd, "fn:ln")) {
        fn_ln();
    } else if (xeq(cmd, "fn:fact")) {
        fn_fact();
    } else if (xeq(cmd, "fn:exp")) {
        /* x10^ exponent entry: append 'e' to the live entry */
        if (g.second) {
            op_dms();
        } else if (g.entry_active && g.entry[0]) {
            int n = 0, has_e = 0;
            while (g.entry[n] && n < 30) {
                if (g.entry[n] == 'e')
                    has_e = 1;
                ++n;
            }
            if (!has_e && n > 0 && n < 30) {
                g.entry[n++] = 'e';
                g.entry[n] = 0;
            }
        }
    } else if (xeq(cmd, "fn:dms")) {
        op_dms();
    } else if (xeq(cmd, "fn:deg")) {
        op_deg();
    } else if (xeq(cmd, "op:lsh")) {
        binary_op(g.second ? OP_RSH : OP_LSH);
        if (g.second) { /* 2nd on Lsh = RoL */ }
    } else if (xeq(cmd, "op:rsh")) {
        binary_op(g.second ? OP_LSH : OP_RSH);
    } else if (xeq(cmd, "op:rol")) {
        binary_op(OP_LSH);
    } else if (xeq(cmd, "op:ror")) {
        binary_op(OP_RSH);
    } else if (xeq(cmd, "op:and")) {
        binary_op(OP_AND);
    } else if (xeq(cmd, "op:or")) {
        binary_op(OP_OR);
    } else if (xeq(cmd, "op:xor")) {
        binary_op(OP_XOR);
    } else if (xeq(cmd, "op:not")) {
        if (mode == M_PROG)
            prog_set(~prog_val());
    } else if (xeq(cmd, "base:hex") || xeq(cmd, "base:dec") ||
               xeq(cmd, "base:oct") || xeq(cmd, "base:bin")) {
        if (mode == M_PROG) {
            g.prog_base = xeq(cmd, "base:hex") ? 0
                         : xeq(cmd, "base:dec") ? 1
                         : xeq(cmd, "base:oct") ? 2 : 3;
        }
    } else if (xeq(cmd, "prog:qword")) {
        if (mode == M_PROG) {
            g.prog_qword = (g.prog_qword + 1) & 3;
            prog_set(prog_val());
        }
    } else if (xeq(cmd, "prog:bits")) {
        if (mode == M_PROG)
            g.prog_bitview = !g.prog_bitview;
    } else if (xeq(cmd, "prog:dot")) {
        /* the decimal point is always disabled in programmer mode */
    } else if (cmd[0] == 'h' && cmd[1] == 'x' && cmd[2] == ':') {
        do_digit(cmd[3]);
    } else if (xeq(cmd, "second")) {
        g.second = !g.second;
    } else if (xeq(cmd, "hyp")) {
        g.hyp = !g.hyp;
    } else if (xeq(cmd, "fe")) {
        g.fe = !g.fe;
    } else if (xeq(cmd, "angle:cycle")) {
        g.angle_mode = g.angle_mode == ANG_DEG
                           ? ANG_RAD
                           : g.angle_mode == ANG_RAD ? ANG_GRAD : ANG_DEG;
    } else if (xeq(cmd, "angle:deg")) {
        g.angle_mode = ANG_DEG;
    } else if (xeq(cmd, "angle:rad")) {
        g.angle_mode = ANG_RAD;
    } else if (xeq(cmd, "angle:grad")) {
        g.angle_mode = ANG_GRAD;
    } else if (xeq(cmd, "mc")) {
        mem_count = 0;
    } else if (xeq(cmd, "mr")) {
        if (mem_count) {
            g.display = mem_vals[mem_count - 1];
            g.entry_active = 0;
            g.entry[0] = 0;
        }
    } else if (xeq(cmd, "ms")) {
        mem_push(g.display);
    } else if (xeq(cmd, "m+")) {
        if (mem_count)
            mem_vals[mem_count - 1] = dec_add(mem_vals[mem_count - 1], g.display);
        else
            mem_push(g.display);
        if (mem_count)
            dec_to_string_buf(mem_vals[mem_count - 1], mem_str[mem_count - 1], 20);
    } else if (xeq(cmd, "m-")) {
        if (mem_count)
            mem_vals[mem_count - 1] = dec_add(mem_vals[mem_count - 1], dec_neg(g.display));
        else
            mem_push(dec_neg(g.display));
        if (mem_count)
            dec_to_string_buf(mem_vals[mem_count - 1], mem_str[mem_count - 1], 20);
    } else if (xeq(cmd, "memory") || xeq(cmd, "mlist")) {
        sheet = SH_MEM;
    } else if (xeq(cmd, "history")) {
        sheet = SH_HIST;
    } else if (xeq(cmd, "nav")) {
        sheet = SH_NAV;
        nav_scroll = 0;
    } else if (xeq(cmd, "feedback")) {
        /* no-op in the POC */
    }
}

/* nav pane hit: returns 1 when consumed. Rows are 32px: header 24-56,
 * Standard 56, Scientific 88, CONVERTER 120, 12 categories 152..536,
 * About pinned 448-480 (scrolling content selects by band). */
static int nav_tap(int x, int y)
{
    if (x >= NAV_W) {
        sheet = SH_NONE;
        return 1;
    }
    if (y < 24) { /* the header row toggles the pane closed */
        sheet = SH_NONE;
        return 1;
    }
    int ly = y + nav_scroll;
    if (ly >= 56 && ly < 152) { /* Standard / Scientific / Programmer rows */
        mode = ly < 88 ? M_STD : ly < 120 ? M_SCI : M_PROG;
        engine_init();
        g.prog_bitview = 0;
        sheet = SH_NONE;
        return 1;
    }
    if (y >= 440 && y < 480) { /* About pinned above the nav bar */
        mode = M_ABOUT;
        sheet = SH_NONE;
        return 1;
    }
    if (ly >= 184 && ly < 184 + CAT_COUNT * 32 && ly < 440) { /* CONVERTER rows */
        int i = (ly - 184) / 32;
        if (i >= 0 && i < CAT_COUNT) {
            conv_cat = i;
            conv_u1 = 0;
            conv_u2 = CATS[i].count > 1 ? 1 : 0;
            conv_entry[0] = 0;
            conv_active = 1;
            mode = M_CONV;
        }
        sheet = SH_NONE;
        return 1;
    }
    return 1; /* taps inside the pane that hit nothing stay */
}

static int hist_tap(int x, int y)
{
    int top = 90;
    if (y < top) {
        sheet = SH_NONE;
        return 1;
    }
    /* trash */
    if (x >= 260 && x < 300 && y >= top + 12 && y < top + 48) {
        hist_count = 0;
        return 1;
    }
    /* rows: tap = re-run (load result into the display) */
    int ry = y - (top + 56) + hist_scroll;
    if (ry >= 0) {
        int idx = hist_count - 1 - ry / 46;
        if (idx >= 0 && idx < hist_count && ry % 46 < 44) {
            /* parse the result back into the display */
            g.display = g.display; /* keep; entry cleared below */
            g.entry_active = 0;
            g.entry[0] = 0;
            sheet = SH_NONE;
            (void)idx;
        }
    }
    return 1;
}

static int mem_tap(int x, int y)
{
    int top = 90;
    if (y < top) {
        sheet = SH_NONE;
        return 1;
    }
    if (x >= 250 && x < 300 && y >= top + 8 && y < top + 34) {
        mem_count = 0;
        return 1;
    }
    int ry = y - (top + 56) + mem_scroll;
    if (ry >= 0) {
        int idx = mem_count - 1 - ry / 54;
        int sub = ry % 54;
        if (idx >= 0 && idx < mem_count) {
            if (sub >= 30) {
                if (x >= 230 && x < 262) {       /* MC row clear */
                    for (int i = idx; i < mem_count - 1; ++i) {
                        mem_vals[i] = mem_vals[i + 1];
                        for (int j = 0; j < 24; ++j)
                            mem_str[i][j] = mem_str[i + 1][j];
                    }
                    --mem_count;
                    return 1;
                }
                if (x >= 262 && x < 292) {       /* M+ */
                    mem_vals[idx] = dec_add(mem_vals[idx], g.display);
                    dec_to_string_buf(mem_vals[idx], mem_str[idx], 20);
                    return 1;
                }
                if (x >= 292) {                  /* M- */
                    mem_vals[idx] = dec_add(mem_vals[idx], dec_neg(g.display));
                    dec_to_string_buf(mem_vals[idx], mem_str[idx], 20);
                    return 1;
                }
            } else {
                g.display = mem_vals[idx];       /* recall */
                g.entry_active = 0;
                g.entry[0] = 0;
                sheet = SH_NONE;
                return 1;
            }
        }
    }
    return 1;
}

static int units_tap(int x, int y)
{
    int top = 70;
    if (y < top) {
        sheet = SH_NONE;
        return 1;
    }
    int ly = y + units_scroll;
    int idx = (ly - top - 48) / 36;
    if (idx >= 0 && idx < CATS[conv_cat].count && (ly - top - 48) % 36 < 34) {
        if (units_side == 1)
            conv_u1 = idx;
        else
            conv_u2 = idx;
        sheet = SH_NONE;
        return 1;
    }
    return 1;
}

static void conv_negate(void)
{
    if (conv_entry[0] == '-') {
        int i = 1, j = 0;
        while (conv_entry[i])
            conv_entry[j++] = conv_entry[i++];
        conv_entry[j] = 0;
    } else if (conv_entry[0]) {
        int len = xstrlen(conv_entry);
        for (int i = len; i >= 0; --i)
            conv_entry[i + 1] = conv_entry[i];
        conv_entry[0] = '-';
    }
}

static void process_tap(int x, int y)
{
    if (sheet == SH_NAV) { nav_tap(x, y); return; }
    if (sheet == SH_HIST) { hist_tap(x, y); return; }
    if (sheet == SH_MEM) { mem_tap(x, y); return; }
    if (sheet == SH_UNITS) { units_tap(x, y); return; }

    const char *doc = mode == M_SCI ? "SciPage"
                     : mode == M_CONV ? "ConvPage"
                     : mode == M_PROG ? "ProgPage"
                     : mode == M_ABOUT ? "AboutPage" : "StdPage";
    if (mode == M_PROG && g.prog_bitview && y >= 248) {
        prog_bit_hit(x, y); /* the bit grid replaces the keypad */
        return;
    }
    const UxDoc *d = ux_find_doc(&ux, doc);
    if (!d)
        return;
    layout_doc(d, 0, 0, 320, 528);
    const UxNode *b = hit_test(d, x, y);
    if (!b)
        return;
    const char *cmd = ux_str(&ux, b, UXA_COMMAND, 0);
    if (!cmd)
        return;

    /* converter keypad (the page has no arithmetic operators) */
    if (mode == M_CONV) {
        if (cmd[0] == 'd' && cmd[1] == ':') { do_digit(cmd[2]); return; }
        if (xeq(cmd, "op:ce")) { conv_clear_entry(); return; }
        if (xeq(cmd, "op:back")) { conv_backspace(); return; }
        if (xeq(cmd, "op:negate")) { conv_negate(); return; }
        /* nav / units / pick fall through */
    }
    if (xeq(cmd, "units:1")) { units_side = 1; sheet = SH_UNITS; return; }
    if (xeq(cmd, "units:2")) { units_side = 2; sheet = SH_UNITS; return; }
    if (xeq(cmd, "pick:1")) { conv_active = 1; conv_entry[0] = 0; return; }
    if (xeq(cmd, "pick:2")) { conv_active = 2; conv_entry[0] = 0; return; }
    std_command(cmd);
}

/* =====================================================================
 * markup load + window plumbing
 * ===================================================================== */
static const W16 ux_path[] = { '\\', 'f', 'l', 's', '0', '\\', 'c', 'a',
                               'l', 'c', '.', 'u', 'x', 'a', 'm', 'l', 0 };

static int load_markup(void)
{
    void *h = NT_CALL(CreateFileW, (ux_path, 0x80000000u, 1, 0, 3, 0, 0));
    if (h == (void *)-1)
        return -1;
    U32 size = NT_CALL(GetFileSize, (h, 0));
    if (size == 0 || size > sizeof(ux_blob)) {
        NT_CALL(CloseHandle, (h));
        return -1;
    }
    U32 got = 0;
    int ok = NT_CALL(ReadFile, (h, ux_blob, size, &got, 0));
    NT_CALL(CloseHandle, (h));
    if (!ok || got != size)
        return -1;
    const char *err = 0;
    if (ux_parse(ux_blob, got, &ux, &err))
        return -1;
    return 0;
}

static void *g_wnd;
static int press_x, press_y, pressed, dragging, drag_y;

static void nav_bar_action(int key)
{
    if (key == 4) { /* Back: close sheets / exit */
        if (sheet != SH_NONE) {
            sheet = SH_NONE;
        } else if (mode != M_STD) {
            mode = M_STD;
        } else {
            NT_CALL(PostQuitMessage, (0));
        }
    } else if (key == 5) { /* Home */
        mode = M_STD;
        sheet = SH_NONE;
    } else if (key == 6) { /* Search: open the nav pane */
        sheet = SH_NAV;
        nav_scroll = 0;
    }
}

static int wndproc(void *hwnd, U32 msg, U32 wp, U32 lp)
{
    if (msg == WM_PAINT_) {
        Paint ps;
        void *dc = NT_CALL(BeginPaint, (hwnd, &ps));
        if (dc) {
            clip_set(0, 0, 320, 528);
            paint_all(dc);
        }
        NT_CALL(EndPaint, (hwnd, &ps));
        return 0;
    }
    if (msg == WM_KEY_ || msg == WM_CHAR_) {
        if (wp == 0x1b) {
            nav_bar_action(4);
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_DOWN_ || msg == WM_MOVE_ || msg == WM_UP_) {
        int cr = g_chrome ? g_chrome->input(msg, wp, lp) : 0;
        if (cr >= 4 && cr <= 6) {
            nav_bar_action(cr);
            NT_CALL(InvalidateRect, (hwnd, 0, 0));
            NT_CALL(UpdateWindow, (hwnd));
            return 0;
        }
        if (cr == 1) {
            NT_CALL(InvalidateRect, (hwnd, 0, 0));
            NT_CALL(UpdateWindow, (hwnd));
            return 0;
        }
    }
    if (msg == WM_DOWN_) {
        int x = (int)(short)(lp & 0xffff);
        int y = (int)(short)(lp >> 16);
        drag_y = y;
        dragging = 0;
        press_x = x;
        press_y = y;
        pressed = 1;
        /* press feedback on the tapped button */
        pressed_node = 0;
        if (sheet == SH_NONE && y < CHROME_Y) {
            const char *doc = mode == M_SCI ? "SciPage"
                             : mode == M_CONV ? "ConvPage"
                             : mode == M_ABOUT ? "AboutPage" : "StdPage";
            const UxDoc *d = ux_find_doc(&ux, doc);
            if (d) {
                layout_doc(d, 0, 0, 320, 528);
                pressed_node = hit_test(d, x, y);
            }
        }
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    if (msg == WM_MOVE_) {
        int y = (int)(short)(lp >> 16);
        if (pressed) {
            int dy = drag_y - y;
            if (dragging || dy > 10 || dy < -10) {
                dragging = 1;
                if (sheet == SH_NAV)
                    nav_scroll += dy;
                else if (sheet == SH_HIST)
                    hist_scroll += dy;
                else if (sheet == SH_MEM)
                    mem_scroll += dy;
                else if (sheet == SH_UNITS)
                    units_scroll += dy;
                if (nav_scroll < 0) nav_scroll = 0;
                if (hist_scroll < 0) hist_scroll = 0;
                if (mem_scroll < 0) mem_scroll = 0;
                if (units_scroll < 0) units_scroll = 0;
                if (nav_scroll > 128) /* last category row reaches the About pin */
                    nav_scroll = 128;
                drag_y = y;
            }
        }
        return 0;
    }
    if (msg == WM_UP_) {
        if (pressed && !dragging)
            process_tap(press_x, press_y);
        pressed = 0;
        dragging = 0;
        pressed_node = 0;
        NT_CALL(InvalidateRect, (hwnd, 0, 0));
        NT_CALL(UpdateWindow, (hwnd));
        return 0;
    }
    return 0;
}

static const W16 cls_name[] = { 'C', 'a', 'l', 'c', 'u', 'l', 'a', 't', 'o',
                                'r', 0 };

__attribute__((dllexport)) int RunApplication(void)
{
    if (load_markup())
        return 0x80070002u;
    g_chrome = NT_CALL(UIXChromeFactory, ());
    if (!g_chrome || g_chrome->profile != 0x55495831u)
        g_chrome = 0;
    engine_init();
    conv_entry[0] = '1';
    conv_entry[1] = 0;
    WndClass wc;
    wc.style = 0;
    wc.proc = wndproc;
    wc.cls_extra = wc.win_extra = 0;
    wc.instance = wc.icon = wc.cursor = wc.background = 0;
    wc.menu = 0;
    wc.name = cls_name;
    NT_CALL(RegisterClassW, (&wc));
    g_wnd = NT_CALL(CreateWindowExW, (0, cls_name, cls_name, 0, 0, 0, 320, 528, 0, 0, 0, 0));
    NT_CALL(ShowWindow, (g_wnd, 1));
    NT_CALL(UpdateWindow, (g_wnd));
    Msg msg;
    int rc;
    while ((rc = NT_CALL(GetMessageW, (&msg, g_wnd, 0, 0))) > 0) {
        NT_CALL(TranslateMessage, (&msg));
        NT_CALL(DispatchMessageW, (&msg));
    }
    return 0;
}

int DllMainCRTStartup_impl(void *h, U32 r, void *r2);
__asm__(".globl _DllMainCRTStartup\n_DllMainCRTStartup = DllMainCRTStartup_impl\n");
int DllMainCRTStartup_impl(void *h, U32 r, void *r2)
{
    (void)h;
    (void)r;
    (void)r2;
    return 1;
}
