#!/usr/bin/env python3
"""Validate a real EX-word gint D01 and its optional JP/CN installation."""
import pathlib
import sys
from package_exword_flat import check as check_payloads

build = pathlib.Path(sys.argv[1])
pe_dist = pathlib.Path(sys.argv[2]) if len(sys.argv) >= 3 else None
native = (build / "ntos_sh.d01").read_bytes()
assert native.startswith(b"CASIODICPLUGIN10"), "wrong native EX-word D01 magic"
assert 128 < len(native) <= 128 * 1024, "EX-word D01 exceeds 128 KiB"
assert (build / "NTOS.d01").read_bytes() == native, "NTOS alias is stale"
for locale in ("ja", "cn"):
    dest = build / "packages" / locale / "NTOSH"
    if not dest.exists():
        continue
    assert (dest / "ntos_sh.d01").read_bytes() == native
    assert (dest / "fileinfo.cji").is_file()
    assert not (dest / "SYSROOT").exists()
    assert not (dest / "Windows").exists()
    if pe_dist is not None:
        check_payloads(pe_dist, dest)
    assert any(dest.glob("*.htm")), "missing EX-word installer metadata"
    assert all(b"@APP" not in h.read_bytes() for h in dest.glob("*.htm"))
print(f"PASS NTOSH direct D01 {len(native)} / 131072 bytes, no EDL stage 2")
