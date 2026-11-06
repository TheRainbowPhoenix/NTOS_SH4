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
    args=sys.argv[1:]
    if len(args)>=2 and args[0]=="--live":
        subprocess.run([str(exe),args[1],"live"],check=True)
    else:
        for path in args:
            subprocess.run([str(exe),path],check=True)
