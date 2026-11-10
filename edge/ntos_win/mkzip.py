#!/usr/bin/env python3
"""mkzip.py — portable zip packager (python3 stdlib only).

The CI runner image ships no `zip` binary (`make package` died with
"zip: not found"); python3 is guaranteed there (the workflows assert the
stdlib). Usage:

    python3 mkzip.py <output.zip> <tree>

Entries are the tree's files at archive-root level (same layout the old
`cd tree && zip -r ../out.zip .` produced), deflated, sorted, with a
fixed timestamp so the artifact is byte-reproducible for a given tree.
run-edge.bat keeps its CRLF bytes verbatim.
"""
import os
import sys
import zipfile

FIXED_DATE = (2025, 1, 1, 0, 0, 0)


def main():
    if len(sys.argv) != 3:
        print('usage: mkzip.py <output.zip> <tree>', file=sys.stderr)
        return 2
    out, root = sys.argv[1], sys.argv[2]
    if not os.path.isdir(root):
        print(f'mkzip: not a directory: {root}', file=sys.stderr)
        return 1
    names = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for fn in sorted(filenames):
            rel = os.path.relpath(os.path.join(dirpath, fn), root)
            names.append(rel.replace(os.sep, '/'))
    with zipfile.ZipFile(out, 'w') as z:
        for n in names:
            zi = zipfile.ZipInfo(n, date_time=FIXED_DATE)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            with open(os.path.join(root, n), 'rb') as f:
                z.writestr(zi, f.read())
    print(f'mkzip: {out} ({len(names)} files)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
