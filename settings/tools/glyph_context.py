#!/usr/bin/env python3
"""glyph_context.py — dump context around MDL2 glyph strings in the
SystemSettings PE files: find code that pushes the glyph string VA and the
string-resource IDs / nearby data to reconstruct the category -> glyph map.

Strategy: parse the PE sections, convert file offsets of glyph strings to
VAs, then scan .text for x86 `push imm32` / `mov [..], imm32` encodings
containing those VAs; dump the surrounding 128 bytes of code hex plus any
imm32 values in the window that look like resource IDs (0x0001..0x8000).
Also dump UTF-16 strings referenced in the same window.
"""
import struct, sys

def parse_pe(path):
    d = open(path, 'rb').read()
    e_lfanew = struct.unpack_from('<I', d, 0x3c)[0]
    assert d[e_lfanew:e_lfanew+4] == b'PE\x00\x00'
    coff = e_lfanew + 4
    nsec, = struct.unpack_from('<H', d, coff + 2)
    opt_size, = struct.unpack_from('<H', d, coff + 16)
    opt = coff + 20
    magic, = struct.unpack_from('<H', d, opt)
    image_base = struct.unpack_from('<Q', d, opt + 24)[0] if magic == 0x20b else \
                 struct.unpack_from('<I', d, opt + 28)[0]
    secs = []
    so = opt + opt_size
    for i in range(nsec):
        name = d[so:so+8].rstrip(b'\0').decode('latin1')
        vsize, va, rsize, ro = struct.unpack_from('<IIII', d, so + 8)
        secs.append((name, va, vsize, ro, rsize))
        so += 40
    return d, image_base, secs

def off_to_va(off, image_base, secs):
    for name, va, vsize, ro, rsize in secs:
        if ro <= off < ro + rsize:
            return image_base + va + (off - ro)
    return None

def va_to_off(va, image_base, secs):
    rva = va - image_base
    for name, sva, vsize, ro, rsize in secs:
        if sva <= rva < sva + vsize:
            return ro + (rva - sva)
    return None

def utf16_strings_in(d, off, length, minlen=3):
    out = []
    i = off
    end = off + length
    while i < end - 2:
        if d[i+1] == 0 and 0x20 <= d[i] < 0x7f:
            j = i
            s = []
            while j < end - 1:
                c = d[j] | (d[j+1] << 8)
                if 0x20 <= c < 0x7f or 0xE700 <= c <= 0xE7FF:
                    s.append(chr(c)); j += 2
                else:
                    break
            if len(s) >= minlen:
                out.append((i, ''.join(s)))
                i = j
                continue
        i += 1
    return out

def main(path):
    d, base, secs = parse_pe(path)
    text = next(s for s in secs if s[0] == '.text')
    tname, tva, tvsize, tro, trsize = text

    # find glyph strings (utf16, PUA) in the whole image
    glyphs = []
    i = 0
    while i < len(d) - 1:
        if d[i+1] == 0xE7:
            va = off_to_va(i, base, secs)
            if va:
                glyphs.append((i, va))
            i += 2
        else:
            i += 1
    print(f'{len(glyphs)} glyph-string offsets')

    # scan .text for references
    t = d[tro:tro+trsize]
    hits = {}
    for k in range(len(t) - 6):
        # push imm32 (0x68 xx xx xx xx) or mov imm32 forms (c7 ... / a1 / 8b 0d etc.)
        if t[k] == 0x68 or t[k] == 0xb8 or (t[k] == 0xc7 and k > 3):
            for off, va in glyphs:
                if struct.unpack_from('<I', t, k + 1)[0] == va:
                    hits.setdefault(va, []).append(tro + k)
    print(f'{len(hits)} glyphs referenced from .text')

    for va, refs in sorted(hits.items()):
        for r in refs:
            # window around the reference
            w0 = max(0, r - 64 - tro)
            w1 = min(trsize, r - tro + 96)
            win = t[w0:w1]
            # collect imm32 candidates in window
            imms = []
            for j in range(len(win) - 4):
                v = struct.unpack_from('<I', win, j)[0]
                if 1 <= v <= 0x10000:
                    imms.append(v)
            # utf16 strings near the ref in the raw file
            strs = utf16_strings_in(d, max(0, r - 400), 800, 4)
            vaoff = va_to_off(va, base, secs)
            gtxt = d[vaoff:vaoff+8].decode('utf-16le', 'ignore').rstrip('\0')
            print(f'\n=== glyph {gtxt!r} (va {va:#x}) ref at file {r:#x}')
            print('  imm32 (1..0x10000):', [hex(x) for x in sorted(set(imms))][:24])
            for so, s in strs:
                print(f'  str@{so:#x}: {s!r}')

if __name__ == '__main__':
    main(sys.argv[1])
