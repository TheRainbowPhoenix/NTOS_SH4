#!/usr/bin/env python3
"""fix_engine3.py —
1. dec_mul: exact 16-digit split multiply (8+8 digit halves, three partial
   products summed through dec_add) instead of lossy 9-digit pre-trim.
2. dec_sqrt: the decimal Newton polish must divide by the RAW mantissa
   root (dec_i(r)), not the scaled Dec — that was corrupting every sqrt
   whose exponent-evening shifted the value (e.g. sqrt(1.17..) fed the
   atan reduction garbage).
"""
import re

SRC = '/home/z/my-project/NTOS_SH4/calc/app/calc_app.c'
d = open(SRC).read()

# ---- 1. dec_mul: split multiply ----
m = re.search(
    r'static int dec_ull_len\(unsigned long long m\)\n\{.*?\n\}\n\n'
    r'static Dec dec_mul\(Dec a, Dec b\)\n\{.*?\n\}\n', d, re.S)
assert m, 'dec_mul block not found'
new_mul = '''static Dec dec_mul(Dec a, Dec b)
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
'''
d = d[:m.start()] + new_mul + d[m.end():]

# ---- 2. dec_sqrt: raw-mantissa Newton polish ----
m = re.search(
    r'    /\* polish in decimal space: 3 Newton steps give 16 digits \*/\n'
    r'    Dec out;\n'
    r'    out\.m = r;\n'
    r'    out\.e = e2;\n'
    r'    for \(int i = 0; i < 3; \+\+i\) \{\n'
    r'        if \(out\.m == 0\)\n'
    r'            break;\n'
    r'        Dec rq = dec_div\(dec_i\(m\), out\);\n'
    r'        out = dec_div_i\(dec_add\(out, rq\), 2\);\n'
    r'    \}\n'
    r'    return dec_trim\(dec_norm\(out\), DECSIG\);\n', d)
assert m, 'sqrt polish not found'
new_polish = '''    /* polish: Newton on the RAW mantissa as a decimal value, then
     * re-apply the (even) half exponent */
    Dec qm = dec_i(r);
    for (int i = 0; i < 3; ++i) {
        if (dec_is_zero(qm))
            break;
        Dec rq = dec_div(dec_i(m), qm);
        qm = dec_div_i(dec_add(qm, rq), 2);
    }
    Dec scale;
    scale.m = 1;
    scale.e = e2;
    return dec_trim(dec_norm(dec_mul(qm, scale)), DECSIG);
'''
d = d[:m.start()] + new_polish + d[m.end():]

open(SRC, 'w').write(d)
print('mul split + sqrt polish fixed')
