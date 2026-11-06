#!/usr/bin/env python3
"""Host-only fixed-base WinCE PE loader regression."""
from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    exe=Path(temp)/"loader_test"
    subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror",
        str(root/"tests/ce_pe_test.cpp"),"-o",str(exe)],check=True)
    for endian in ("le","be"):
        image=Path(temp)/("synthetic_"+endian+".exe")
        subprocess.run([sys.executable,str(root/"tools/make_ce_poc.py"),
                        str(image),endian],check=True)
        subprocess.run([str(exe),str(image),"imports"],check=True)
    for path in sys.argv[1:]:
        subprocess.run([str(exe),path],check=True)
