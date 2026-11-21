#!/usr/bin/env python3
"""SH4BE PE32 checks: GWES/SIP/WinCE API exports must not be COREDLL stubs."""
import sys
from check_gdi_import_exports import PE

GWES = {
    "NtosGwesCreate", "NtosGwesClose", "NtosGwesSetMode",
    "NtosGwesGetMode", "NtosGwesSwap", "NtosGwesFocus",
    "NtosGwesGetRect", "NtosGwesHit", "NtosGwesKeyboard",
    "NtosGwesReset",
}
GDI = {
    "StretchBlt", "SetPixel", "GetPixel", "FillRect", "GetDC",
    "ReleaseDC", "CreateCompatibleDC", "CreateCompatibleBitmap",
    "SelectObject", "DeleteObject", "DeleteDC", "CreateSolidBrush",
}
CE = {
    "SipShowIM", "SipGetInfo", "SipSetInfo", "CreateWindowExW",
    "GetMessageW", "DispatchMessageW", "SetWindowTextW",
    "GetWindowTextW", "ShowWindow", "DestroyWindow",
}

def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: check_gwes_exports.py COREDLL.DLL GWESCHK.EXE GWESLAB.EXE")
    dll=PE(sys.argv[1])
    table=dll.exports()
    stub=table["AbortDoc"]
    required=GWES|GDI|CE
    for symbol in sorted(required):
        if not table.get(symbol) or table[symbol]==stub:
            raise ValueError(f"GWES unresolved or STUB: {symbol}")
    probe=PE(sys.argv[2]).imports().get("COREDLL.DLL",set())
    if probe!=GWES|GDI:
        raise ValueError(f"GWESCHK IAT mismatch: missing={sorted((GWES|GDI)-probe)} extra={sorted(probe-(GWES|GDI))}")
    lab=PE(sys.argv[3]).imports().get("COREDLL.DLL",set())
    if not GWES.issubset(lab) or not {"SipShowIM","CreateWindowExW","GetMessageW","DispatchMessageW"}.issubset(lab):
        raise ValueError("GWESLAB missing actual GWES/SIP/WinCE message imports")
    print(f"PASS GWES SH4BE PE32: {len(required)} non-stub WinCE+GWES+GDI exports, {len(probe)} probe imports, {len(lab)} live window/SIP demo imports")

if __name__=="__main__":
    main()
