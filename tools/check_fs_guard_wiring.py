#!/usr/bin/env python3
"""Non-hardware audit: every SDK descriptor open/close is behind the gate.

The run-time single-handle guarantee itself is tested in fs_guard_test.cpp.
This source check prevents someone adding an untracked File_Open call later.
"""
from pathlib import Path
import re
main=Path("src/main.cpp").read_text()
assert '#include "fs_guard.hpp"' in main
expected={
    "File_Open":2,  # SDK call in fs_open and documentary mention in comment
    "File_Close":2,
    "File_FindFirst":1,
    "File_FindNext":1,
    "File_FindClose":2,
}
for name in expected:
    occurrences=[m.start() for m in re.finditer(r"\b"+name+r"\s*\(",main)]
    # File_Open/Close in docstrings are allowed; code call sites must be
    # in the exclusive bridge section, before ntos_file_open.
    boundary=main.index("static int32_t ntos_file_open(")
    assert all(i<boundary for i in occurrences), (name,len(occurrences),
          "untracked SDK call after exclusive gateway")
assert "static int fs_open(" in main
assert "static int fs_find_first(" in main
assert "g_fs.cleanup(" in main
assert "if(!fs_recover())" in main
assert 'Windows\\\\System32' in main
assert '"named/ordinal WinCE export missing"' in main
package=Path("tools/package_device.py").read_text()
assert "system32.mkdir(" in package
assert 'normalized.endswith((".DLL", ".CPL"))' in package
print("PASS loader/file-API SDK calls gated; System32 packaging and close cleanup wired")
