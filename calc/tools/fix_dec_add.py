#!/usr/bin/env python3
"""fix_dec_add.py — replace the broken exponent alignment in dec_add with a
correct one: scale the coarser operand UP when it fits (exact), otherwise
align the finer operand down (truncated below the coarse scale)."""
import re

SRC = '/home/z/my-project/NTOS_SH4/calc/app/calc_app.c'
d = open(SRC).read()

m = re.search(r'static Dec dec_add\(Dec a, Dec b\)\n\{.*?\n\}\n', d, re.S)
assert m, 'dec_add not found'

new = '''static Dec dec_add(Dec a, Dec b)
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
'''

d = d[:m.start()] + new + d[m.end():]
open(SRC, 'w').write(d)
print('dec_add replaced')
