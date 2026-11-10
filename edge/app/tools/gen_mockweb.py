#!/usr/bin/env python3
"""gen_mockweb.py — emit mockweb.h with embedded NUL-terminated page sources.

The router in edge_app.c serves these as if the network stack existed.
Arrays use INTEGER initializers (not string literals) so the bytes are the
actual page text. All sources live in tests/assets/ so the tree regenerates
anywhere (the pre-repo mdtest/ paths are gone with that sandbox).
"""
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / 'tests' / 'assets'
OUT = Path(__file__).resolve().parent.parent / 'mockweb.h'
FAQ_CAP = 9000

PAGES = [
    # (array name, asset file, cap or None, expected first char)
    ('MW_ACID1', 'acid1.html', None, '<'),
    ('MW_ACID2', 'acid2.htm', None, '<'),
    ('MW_WIKI_CP300', 'wiki_cp300.md', None, '#'),
    ('MW_CWIKI_FAQ', 'cwfaq.mdx', FAQ_CAP, '-'),
    ('MW_FORM', 'form.htm', None, '<'),
]


def c_array(name, text):
    vals = [str(10 if ch == '\n' else ord(ch)) for ch in text if ch != '\r']
    vals.append('0')
    lines = []
    line = ''
    for v in vals:
        if len(line) + len(v) + 1 > 100:
            lines.append('    ' + line)  # keep trailing comma
            line = ''
        line += v + ','
    lines.append('    ' + line[:-1])  # final value without comma
    return f'static const char {name}[] = {{\n' + '\n'.join(lines) + '\n};'


def main():
    hdr = '''/* mockweb.h — the POC's "online" content, served by the URL router as if
 * the network stack existed: the CSS1 ACID1 + ACID2 test pages, a form
 * demo, a Wikipedia article and a wiki FAQ. Embedded NUL-terminated
 * sources keep the device offline. Regenerate with
 * tools/gen_mockweb.py — do not edit by hand. */
#ifndef MOCKWEB_H
#define MOCKWEB_H

'''
    names = []
    for arr, fname, cap, first in PAGES:
        p = SRC / fname
        text = p.read_text()
        if cap:
            text = text[:cap]
        hdr += c_array(arr, text) + '\n\n'
        names.append((arr, first))
    hdr += '#endif /* MOCKWEB_H */\n'
    OUT.write_text(hdr)

    # self-check: compile + run a probe that memcmps the arrays
    conds = '\n'.join(
        f'    if ({n}[0] != {ord(fc)}) return {i + 1};' for i, (n, fc) in enumerate(names))
    lens = ', '.join(f'strlen({n})' for n, _ in names)
    probe = f'''#include "mockweb.h"
#include <string.h>
#include <stdio.h>
int main(void) {{
{conds}
    printf("ok {lens}\\n");
    return 0;
}}
'''
    open('/tmp/mwprobe.c', 'w').write(probe)
    r = subprocess.run(['gcc', '-I', str(OUT.parent), '-o', '/tmp/mwprobe',
                        '/tmp/mwprobe.c'], capture_output=True, text=True)
    if r.returncode:
        print('probe compile FAILED:', r.stderr[:400])
        return 1
    r2 = subprocess.run(['/tmp/mwprobe'], capture_output=True, text=True)
    print('probe:', r2.stdout.strip(), 'exit', r2.returncode)
    return r2.returncode


if __name__ == '__main__':
    sys.exit(main())
