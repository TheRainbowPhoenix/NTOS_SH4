#!/usr/bin/env python3
"""Generate a BE COFF NTX from the in-repo ELF fixture and test C++ parsing."""
from pathlib import Path
import importlib.util
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("elf2ntx", root / "tools" / "elf2ntx.py")
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)
base = packer.DEFAULT_BASE
elf = packer.Elf(packer.make_synthetic_elf(base))
image, sites = packer.emit_ntx(elf, base)
assert sites == [8], sites

with tempfile.TemporaryDirectory() as temp:
    image_path = Path(temp) / "HELLO.NTX"
    binary = Path(temp) / "coff_parser_test"
    image_path.write_bytes(image)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-o", str(binary),
                    str(root / "tests" / "coff_parser_test.cpp")], check=True)
    subprocess.run([str(binary), str(image_path)], check=True)
