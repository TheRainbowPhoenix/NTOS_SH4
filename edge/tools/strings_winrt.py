#!/usr/bin/env python3
"""strings_winrt.py — extract WinRT/XAML-relevant strings from Edge x86 PEs.

Focuses on: runtime class activation names (SpartanXAML.*, SpModel.*,
Windows.UI.Xaml.*), ms-appx resource URIs, x:Name-ish identifiers, styles,
and HSTRING-embedded literals. Output: edge/analysis/strings/<name>.winrt.txt
"""
import sys, os, re

PATTERNS = [
    ("class", re.compile(rb"(?:SpartanXAML|SpModel|SpartanCore|Microsoft\.Edge|EdgeCore|Chakra)[A-Za-z0-9_.]*")),
    ("winrt", re.compile(rb"Windows\.(?:UI|Storage|Foundation|Web|ApplicationModel|System|Security|Data|Devices|Networking|Globalization|Services)[A-Za-z0-9_.]*")),
    ("msappx", re.compile(rb"ms-appx[:/][^\x00\"'<>\s]*")),
    ("res", re.compile(rb"ms-resource:[^\x00\"'<>\s]*")),
    ("xamlfile", re.compile(rb"[A-Za-z0-9_]+\.(?:xaml|xbf|pri|dll)\b")),
    ("prop", re.compile(rb"[A-Za-z0-9]+\.(?:UIElement|FrameworkElement|Grid|TextBlock|Button)\.[A-Za-z]+")),
]


def extract(path):
    data = open(path, 'rb').read()
    name = os.path.basename(path)
    out = {}
    for kind, pat in PATTERNS:
        for m in pat.finditer(data):
            s = m.group().decode('latin1')
            out[(kind, s)] = out.get((kind, s), 0) + 1
    # utf-16 scan: decode as utf-16le over even offsets via regex on interleaved
    # nulls is unreliable; instead decode whole file and re-encode
    try:
        wdata = data.decode('utf-16-le', errors='ignore')
    except Exception:
        wdata = ''
    for kind, pat in PATTERNS:
        for m in pat.finditer(wdata.encode('latin1', errors='ignore')):
            s = m.group().decode('latin1')
            key = (kind + '-u16', s)
            out[key] = out.get(key, 0) + 1
    return name, out


def main():
    outdir = os.path.join(os.path.dirname(__file__), '..', 'analysis', 'strings')
    os.makedirs(outdir, exist_ok=True)
    for path in sys.argv[1:]:
        name, out = extract(path)
        dst = os.path.join(outdir, name + '.winrt.txt')
        with open(dst, 'w') as f:
            for (kind, s), n in sorted(out.items()):
                f.write(f"{kind:12s} {n:3d} {s}\n")
        print(dst, len(out), 'unique')


if __name__ == '__main__':
    main()
