#!/usr/bin/env python3
"""Format and statically check the WinCE C implementation; optionally run native QA."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--format', action='store_true')
    ap.add_argument('--native', action='store_true')
    ap.add_argument('--analyze', action='store_true')
    ap.add_argument('--cc', default=shutil.which('gcc'))
    ap.add_argument('--formatter', default=str(ROOT/'obj/wince/format-tool/clang_format/data/bin/clang-format.exe'))
    args = ap.parse_args()
    files = sorted(str(p) for pattern in ('*.c', '*.h') for p in HERE.rglob(pattern))
    formatter = shutil.which('clang-format') or args.formatter
    if not Path(formatter).exists() and not shutil.which(formatter):
        raise SystemExit('Install clang-format (tested 23.1.5), or pass --formatter PATH')
    subprocess.run([formatter, *(['-i'] if args.format else ['--dry-run','--Werror']), *files], check=True)
    if not args.cc:
        raise SystemExit('Pass --cc with a native GCC or Clang executable')
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic']
    if args.analyze:
        flags += ['-fanalyzer']
    source = HERE/'tests/native_test.c'
    subprocess.run([args.cc, *flags, '-fsyntax-only', str(source)], check=True)
    if args.analyze:
        analysis = ROOT/'obj/wince/native-analysis.o'
        analysis.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([args.cc, *flags, '-c', str(source), '-o', str(analysis)], check=True)
    if args.native:
        out = ROOT/'obj/wince/native-test.exe'
        out.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([args.cc, *flags, '-g', str(source), '-o', str(out)], check=True)
        subprocess.run([str(out)], check=True)
    print(f'PASS: clang-format and strict compiler checks ({len(files)} C/header files)')

if __name__ == '__main__':
    main()
