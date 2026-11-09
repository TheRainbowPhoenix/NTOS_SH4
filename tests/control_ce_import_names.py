#!/usr/bin/env python3
"""Verify reference CONTROL.EXE ordinal imports also carry their known API names.

The original binary has ordinal-only IAT thunks; metadata resolves those
ordinals to public names without changing PE import semantics.
"""
import json
from pathlib import Path
import pefile

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = {
    2: "InitCommonControlsEx",
    3: "CommandBar_Create",
    8: "CommandBar_InsertMenubar",
    10: "CommandBar_AddAdornments",
    12: "CommandBar_Height",
    44: "CommandBar_AlignAdornments",
}
control = json.loads((ROOT / "payload/wince/abi/control.exe.json").read_text())
symbols = next(x["symbols"] for x in control["imports"]
               if x["module"].lower() == "commctrl.dll")
actual = {x["ordinal"]: x["name"] for x in symbols}
assert actual == REFERENCE, (actual, REFERENCE)
reference = json.loads((ROOT / "payload/wince/abi/commctrl.dll.json").read_text())
exports = {x["ordinal"]: x["name"] for x in reference["exports"]}
for ordinal, name in REFERENCE.items():
    assert exports[ordinal] == name, (ordinal, exports.get(ordinal), name)
out = ROOT / "dist_ce_demo/COMMCTRL.DLL"
if out.is_file():
    binary = pefile.PE(str(out))
    emitted = {x.ordinal: x.name.decode() if x.name else None
               for x in binary.DIRECTORY_ENTRY_EXPORT.symbols}
    for ordinal, name in REFERENCE.items():
        assert emitted[ordinal] == name, (ordinal, emitted.get(ordinal), name)
print("PASS six Control Panel COMMCTRL imports: names and ordinals agree")
