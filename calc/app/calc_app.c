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

/* =====================================================================
 * palette: W10M light theme (App.xbf ThemeDictionaries[Light] + the
 * reference-device accent the files/settings ports measured)
 * ===================================================================== */
#define C_PAGE   RGB_(0xF2, 0xF2, 0xF2) /* AltHigh: page + display    */
#define C_SHADE  RGB_(0xE6, 0xE6, 0xE6) /* ChromeLow: numpad shade    */
#define C_SHEET  RGB_(0xDC, 0xDC, 0xDC) /* ChromeMediumLow: sheets    */
#define C_HAIR   RGB_(0xDE, 0xDB, 0xDE) /* hairline                   */
#define C_ACC    RGB_(0x39, 0x65, 0xFF) /* accent (reference device)  */
#define C_INK    RGB_(0x00, 0x00, 0x00)
#define C_MUT    RGB_(0x5D, 0x5A, 0x5C) /* BaseMedium text            */
#define C_PRESS  RGB_(0xDE, 0xDB, 0xDE) /* ListMedium press tint      */
#define C_SCRIM  RGB_(0x54, 0x54, 0x54) /* sheet scrim (alpha-ish)    */
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
enum { OP_NONE = 0, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_YROOT };

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
    /* parse decimal string */
    long long m = 0;
    int e = 0, neg = 0, seen_dot = 0, digits = 0;
    const char *s = g.entry;
    if (*s == '-') {
        neg = 1;
        ++s;
    }
    for (; *s; ++s) {
        if (*s == '.') {
            seen_dot = 1;
            continue;
        }
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
    case OP_POW: return dec_pow(a, b);
    case OP_YROOT: {
        if (dec_is_zero(b))
            return dec_i(0);
        Dec inv = dec_div_i(dec_i(1), b);
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
    /* 2nd toggles inverse for the plain trig buttons */
    if (g.second && !hyp)
        inv = 1;
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
    g.display = dec_div_i(dec_i(1), v);
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
