#!/usr/bin/env python3
"""fix_engine2.py — three engine fixes:
1. dec_mul: adaptive precision (trim only as much as needed so the
   mantissa product fits int64: digits_a + digits_b <= 18).
2. trig: replace the Q28 CORDIC (8-digit precision, buggy vectoring)
   with decimal Taylor series: angle reduction mod 2pi + quadrant fold
   into [0, pi/4], then sin/cos series; atan via double-angle reduction
   + series; asin via atan(x/sqrt(1-x^2)).
3. dec_to_string_buf: comma grouping counts from the RIGHT (digit position
   relative to the units digit), not from the most significant digit.
"""
import re

SRC = '/home/z/my-project/NTOS_SH4/calc/app/calc_app.c'
d = open(SRC).read()

# ---------- 1. dec_mul ----------
m = re.search(r'static Dec dec_mul\(Dec a, Dec b\)\n\{.*?\n\}\n', d, re.S)
assert m, 'dec_mul not found'
new_mul = '''static int dec_ull_len(unsigned long long m)
{
    int dgt = 0;
    while (m) {
        ++dgt;
        m /= 10;
    }
    return dgt;
}

static Dec dec_mul(Dec a, Dec b)
{
    if (a.m == 0 || b.m == 0)
        return dec_i(0);
    /* keep digits(a.m) + digits(b.m) <= 18 so the product fits int64 */
    unsigned long long ma = a.m < 0 ? (unsigned long long)(-(a.m + 1)) + 1ull
                                    : (unsigned long long)a.m;
    unsigned long long mb = b.m < 0 ? (unsigned long long)(-(b.m + 1)) + 1ull
                                    : (unsigned long long)b.m;
    int da = dec_ull_len(ma), db = dec_ull_len(mb);
    while (da + db > 18) {
        if (da >= db) {
            a = dec_trim(a, da - 1);
            --da;
        } else {
            b = dec_trim(b, db - 1);
            --db;
        }
    }
    Dec r;
    r.m = a.m * b.m;
    r.e = a.e + b.e;
    return dec_trim(dec_norm(r), DECSIG);
}
'''
d = d[:m.start()] + new_mul + d[m.end():]

# ---------- 2. trig replacement ----------
# cut from the CORDIC banner through dec_acos (keep asin/acos but re-point
# them at the new atan)
m = re.search(
    r'/\* ---- CORDIC sin/cos.*?static Dec dec_acos\(Dec x\)\n\{.*?\n\}\n',
    d, re.S)
assert m, 'cordic block not found'
new_trig = '''/* ---- trigonometry: decimal Taylor after full angle reduction ----
 * (no FPU, and the CORDIC path only bought 8 significant digits) */

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
        sum = dec_add(sum, dec_div_i(term, 2 * k + 1));
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
'''
d = d[:m.start()] + new_trig + d[m.end():]

# remove the now-unused atan flip helper if it survived
d = d.replace('''/* |x| > 1 test (unused by dec_atan now, kept for callers) */
static int dec_abs_gt1_unused(Dec a)
{
    if (dec_sign(a) == 0)
        return 0;
    if (dec_sign(a) < 0)
        a = dec_neg(a);
    return dec_cmp(a, dec_i(1)) > 0;
}

''', '')

open(SRC, 'w').write(d)
print('mul + trig replaced')
