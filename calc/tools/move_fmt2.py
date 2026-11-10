#!/usr/bin/env python3
"""move_fmt2.py — move the remaining formatter helpers (dec_ull_digits +
dec_to_string_buf) from the engine-state section up before the banner."""
import re

SRC = '/home/z/my-project/NTOS_SH4/calc/app/calc_app.c'
d = open(SRC).read()

# capture from the dec_ull_digits comment through the end of
# dec_to_string_buf (the next function that ends with the mixed-digits
# fraction block: "}\n}\n" right before "static void engine_init")
m = re.search(
    r'/\* digits of \|m\| into tmp\[\].*?\n\}\n\nstatic void engine_init',
    d, re.S)
assert m, 'formatter tail not found'
tail = m.group(0)
d = d.replace(tail, 'static void engine_init', 1)

banner = '/* =====================================================================\n * engine state (standard + scientific)'
i = d.find(banner)
assert i > 0
# insert the tail (minus the engine_init marker we consumed) before banner
block = tail[:-len('static void engine_init')]
d = d[:i] + block + d[i:]
open(SRC, 'w').write(d)
print('moved tail', len(block))
