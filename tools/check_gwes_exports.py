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
    "StretchBlt", "BitBlt", "TextOutW", "DrawTextW", "SetPixel", "GetPixel", "FillRect", "GetDC",
    "ReleaseDC", "CreateCompatibleDC", "CreateCompatibleBitmap",
    "SelectObject", "DeleteObject", "DeleteDC", "CreateSolidBrush",
}
DIALOG = {
    "NtosThemeSet", "NtosThemeGet", "NtosDialogOpen",
    "NtosDialogAdd", "NtosDialogMove", "NtosDialogGetRect",
    "NtosDialogTop", "NtosDialogEnd", "NtosDialogResult",
    "NtosDialogRelease", "NtosDialogEvent", "NtosDialogGetText",
    "NtosDialogSetText", "NtosDialogDraw", "MessageBoxW",
}
CE = {
    "SipShowIM", "SipGetInfo", "SipSetInfo", "CreateWindowExW",
    "GetMessageW", "DispatchMessageW", "SetWindowTextW",
    "GetWindowTextW", "ShowWindow", "DestroyWindow",
}

def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: check_gwes_exports.py COREDLL.DLL GWESCHK.EXE GWESLAB.EXE DLGCHK.EXE")
    dll=PE(sys.argv[1])
    table=dll.exports()
    stub=table["AbortDoc"]
    required=GWES|GDI|CE|DIALOG
    for symbol in sorted(required):
        if not table.get(symbol) or table[symbol]==stub:
            raise ValueError(f"GWES unresolved or STUB: {symbol}")
    probe=PE(sys.argv[2]).imports().get("COREDLL.DLL",set())
    # GDIBLIT/COREDLL implement BitBlt, DrawTextW, TextOutW; the
    # GWESCHK executable intentionally imports only APIs that it calls.
    used = GWES | (GDI - {"BitBlt", "DrawTextW", "TextOutW"})
    if probe != used:
        raise ValueError(f"GWESCHK IAT mismatch: missing={sorted(used-probe)} extra={sorted(probe-used)}")
    lab=PE(sys.argv[3]).imports().get("COREDLL.DLL",set())
    if not GWES.issubset(lab) or not {"SipShowIM","CreateWindowExW","GetMessageW","DispatchMessageW"}.issubset(lab):
        raise ValueError("GWESLAB missing actual GWES/SIP/WinCE message imports")
    dialog=PE(sys.argv[4]).imports().get("COREDLL.DLL",set())
    expected={
        "NtosThemeSet", "NtosThemeGet", "NtosDialogOpen",
        "NtosDialogAdd", "NtosDialogMove", "NtosDialogGetRect",
        "NtosDialogTop", "NtosDialogEnd", "NtosDialogResult",
        "NtosDialogRelease", "NtosDialogSetText", "NtosDialogDraw",
        "NtosGwesCreate", "NtosGwesClose",
        "NtosGwesSetMode", "NtosGwesReset",
    }
    if dialog != expected:
        raise ValueError(f"DLGCHK IAT mismatch: missing={sorted(expected-dialog)} extra={sorted(dialog-expected)}")
    if not {"NtosDialogOpen","NtosDialogAdd","MessageBoxW","NtosThemeSet"}.issubset(lab):
        raise ValueError("GWESLAB missing real modal dialog/MessageBoxW/theme imports")
    print(f"PASS GWES SH4BE PE32: {len(required)} non-stub WinCE/GDI/GWES/dialog exports, {len(probe)} GWESCHK, {len(lab)} GWESLAB, {len(dialog)} DLGCHK imports")

if __name__=="__main__":
    main()
