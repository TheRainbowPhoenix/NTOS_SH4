#!/usr/bin/env python3
"""glyph_ptrs.py — find .rdata pointer entries pointing at glyph strings,
then find .text references to those pointers (VA tables). Dumps nearby
table slots to reconstruct {glyph, ...} rows with their neighbours.
"""
import struct, sys
from glyph_context import parse_pe, off_to_va, va_to_off

def utf16_at(d, off):
    s = []
    while off < len(d) - 1:
        c = d[off] | (d[off+1] << 8)
        if c == 0:
            break
        s.append(chr(c)); off += 2
    return ''.join(s)

def main(path):
    d, base, secs = parse_pe(path)
    # glyph string offsets (utf16 PUA)
    gl = []
    i = 0
    while i < len(d) - 1:
        if d[i+1] == 0xE7:
            va = off_to_va(i, base, secs)
            if va:
                gl.append((i, va))
            i += 2
        else:
            i += 1
    vas = {va: off for off, va in gl}

    # scan ALL sections for 8-byte / 4-byte pointers to these VAs
    ptr_hits = []  # (file_off, ptr_va_target, section)
    for name, sva, vsize, ro, rsize in secs:
        if name == '.text':
            continue
        sec = d[ro:ro+rsize]
        step = 4 if base < (1 << 32) else 8
        for j in range(0, len(sec) - step + 1, step):
            v = struct.unpack_from('<Q' if step == 8 else '<I', sec, j)
            v = v[0]
            if v in vas:
                ptr_hits.append((ro + j, v, name))
    print(f'{len(ptr_hits)} data pointers to glyph strings')
    # group by neighbourhood: dump 64 bytes around each pointer slot
    seen = set()
    for off, va, name in ptr_hits:
        key = off & ~0x3f
        if key in seen:
            continue
        seen.add(key)
        print(f'\n--- {name} @{off:#x} -> glyph {utf16_at(d, vas[va])!r} (va {va:#x})')
        # dump the surrounding 4-byte slots with interpretation
        base_off = off - 32
        for s in range(base_off, base_off + 80, 4):
            if s < 0 or s + 4 > len(d):
                continue
            v = struct.unpack_from('<I', d, s)[0]
            note = ''
            if v in vas:
                note = ' -> glyph ' + repr(utf16_at(d, vas[v]))
            else:
                o2 = va_to_off(v, base, secs) if v > base else None
                if o2:
                    txt = utf16_at(d, o2)
                    if txt and all(0x20 <= ord(c) < 0x7f or 0xE700 <= ord(c) <= 0xE7FF for c in txt) and len(txt) >= 2:
                        note = f' -> str {txt!r}'
            print(f'  {s:#06x}: {v:#010x}{note}')

if __name__ == '__main__':
    main(sys.argv[1])
