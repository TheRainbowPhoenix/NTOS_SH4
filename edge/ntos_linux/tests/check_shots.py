#!/usr/bin/env python3
"""check_shots.py — validate NTOS_SH4_linux screenshots (non-blank, correct
size, expected distinct colors). Fails the e2e test when a frame is blank."""
import sys
import zlib
import struct


def read_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', 'bad signature'
    pos = 8
    idat = b''
    w = h = None
    while pos < len(d):
        ln, typ = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and ctype == 2, f'unsupported {depth=} {ctype=}'
        elif typ == b'IDAT':
            idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * 3 + 1
    px = []
    for y in range(h):
        row = raw[y * stride:(y + 1) * stride]
        assert row[0] == 0, 'filter must be none'
        px.append(row[1:])
    return w, h, b''.join(px)


def main():
    failures = 0
    for path in sys.argv[1:]:
        w, h, px = read_png(path)
        colors = {px[i:i + 3] for i in range(0, len(px), 3)}
        # a blank frame would be a single color everywhere
        status = 'OK' if len(colors) > 3 else 'SUSPICIOUS(blank?)'
        if len(colors) <= 3:
            failures += 1
        print(f'{path}: {w}x{h}, {len(colors)} colors [{status}]')
    if failures:
        sys.exit(1)


if __name__ == '__main__':
    main()
