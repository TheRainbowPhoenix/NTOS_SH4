#!/usr/bin/env python3
"""Apply or enforce Clang format on authored C and H sources.

Reference compiler fixtures and generated bitmap/icon headers are immutable.
Use the same clang-format executable in CI and for local formatting.
"""
import argparse
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SKIP_PREFIXES = ("payload/abi_compare/", "tests/native_pe/")
SKIP_EXACT = {
    "payload/font8x8.h",
    "payload/ugui_font.h",
    "payload/selawik_ui.h",
    "payload/phone/font_bitmap.h",
    "payload/phone/settings_icons.h",
    "payload/phone/system_icons.h",
}


def sources():
    result = subprocess.run(
        ["git", "-c", "safe.directory=*", "ls-files", "-z"],
        cwd=ROOT, check=True, capture_output=True
    )
    paths = sorted(p.decode() for p in result.stdout.split(b"\0") if p)
    return [
        ROOT / p for p in paths
        if p.endswith((".c", ".h"))
        and p not in SKIP_EXACT
        and not any(p.startswith(prefix) for prefix in SKIP_PREFIXES)
    ]


def run_one(executable, path, check):
    args = [executable, "--style=file"]
    if check:
        args += ["--dry-run", "--Werror"]
    else:
        args += ["-i"]
    result = subprocess.run(args + [str(path)], capture_output=True, text=True)
    return str(path.relative_to(ROOT)), result.returncode, result.stderr


def main():
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--apply", action="store_true")
    mode.add_argument("--check", action="store_true")
    args = parser.parse_args()
    executable = os.environ.get("CLANG_FORMAT", "clang-format")
    if not shutil.which(executable):
        parser.error(f"{executable} not found: install clang-format")
    files = sources()
    with ThreadPoolExecutor(max_workers=8) as pool:
        results = list(pool.map(
            lambda path: run_one(executable, path, args.check), files
        ))
    failed = [(p, err) for p, status, err in results if status]
    for path, error in failed[:20]:
        print(path, error.strip()[:800], file=sys.stderr)
    print(f"Clang formatted/checked {len(files)} authored C/H sources; "
          f"{len(failed)} failures")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
