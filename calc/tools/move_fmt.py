#!/usr/bin/env python3
"""move_fmt.py — move the Dec -> display string block from the engine-state
section up into the math section (before the engine banner) so the host
engine test slice includes it."""
import re

SRC = '/home/z/my-project/NTOS_SH4/calc/app/calc_app.c'
d = open(SRC).read()

m = re.search(
    r'/\* =+\n \* Dec -> display string.*?\n\}\n\n', d, re.S)
assert m, 'display string block not found'
block = m.group(0)
d = d.replace(block, '', 1)

banner = '/* =====================================================================\n * engine state (standard + scientific)'
i = d.find(banner)
assert i > 0
d = d[:i] + block + d[i:]

# drop the now-duplicate forward declaration (the one above the alias)
d = d.replace(
    'void dec_to_string_buf(Dec a, char *buf, int bufsize);\n\n'
    '/* alias kept for the series code below */',
    '/* alias kept for the series code below */', 1)

open(SRC, 'w').write(d)
print('moved; block len', len(block))
