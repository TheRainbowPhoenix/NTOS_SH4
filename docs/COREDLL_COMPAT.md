# NTOS COREDLL compatibility layer (experimental)

This COREDLL.DLL is a **Hollyhock-3-hosted WinCE API subset**, not the original
Windows CE kernel. NTOS runs SH4BE PE programs at their preferred virtual
addresses, snaps COREDLL imports, and forwards supported operations to SDK
functions through a fixed-function-pointer API table in the 0x08100000 P0
API window. There is no WinCE process/thread/window-manager implementation.

## Demonstrations

- **PAINT.EXE** — GDI DC, brushes, pixel drawing, and touch via GetMessageW.
- **WINLAB.EXE** — swatch palette, touch drawing, arrow keys and tappable
  **SAVE**/**LOAD** buttons (y=100–132). Saves a 24-byte state record to
  `\\fls0\\NTLAB.DAT`. EXE saves; 1 loads; CLEAR exits. Bottom of toolbar
  displays `SAVED NTLAB.DAT`, `LOADED NTLAB.DAT`, or `ERR 0xXXXXXXXX`.
- **GUIWIN.EXE** — WinCE-style RegisterClassW, CreateWindowExW, WNDPROC,
  WM_PAINT, WM_LBUTTONDOWN / WM_KEYDOWN, message pump, and redraw. Touch
  PREV/NEXT or press left/right; CLEAR exits.
- **MEMTOOL.EXE** — diagnostics via OutputDebugStringW, memory allocation
  and return-code/entry-point checks (now known to work on device).

## Implemented API names

### USER / input and windows

`RegisterClassW`, `CreateWindowExW`, `DestroyWindow`, `ShowWindow`,
`GetClientRect`, `GetSystemMetrics`, `GetMessageW`, `TranslateMessage`,
`DispatchMessageW`, `DefWindowProcW`, `PostMessageW`, `PostQuitMessage`,
`InvalidateRect`, `UpdateWindow`, `SetWindowTextW`, `GetWindowTextW`.

Single window only. A bounded 16-slot posted-message queue provides WM_PAINT
and programmatically posted events. Native Hollyhock touch-down/drag becomes
WM_LBUTTONDOWN / WM_MOUSEMOVE; key press becomes WM_KEYDOWN, with arrows and
EXE mapped to standard WinCE virtual keys. CLEAR terminates GetMessageW via
WM_QUIT. No focus routing, desktop, z-order, modal loops, timers, control
classes, system caret, Unicode IME, or asynchronous OS window scheduler.
GetMessageW currently ignores min/max filters and hwnd-based filtering;
TranslateMessage does not synthesize WM_CHAR events.

### Graphics

`GetDC`, `ReleaseDC`, `BeginPaint`, `EndPaint`, `SetPixel`,
`CreateSolidBrush`, `FillRect`, `DeleteObject`, `MoveToEx`, `LineTo`,
`Rectangle`, `TextOutW`.

A single synthetic full-screen DC; drawing uses RGB565 conversion and SDK
pixels/lines. TextOutW uses the Hollyhock debug small font, not TrueType.
Brushes are color-coded synthetic handles. There is no clipping region,
font selection, bitmap/blit pipeline, actual GDI object ownership, or
window compositing. BeginPaint/EndPaint currently operate on the same DC.

### File I/O

`CreateFileW`, `ReadFile`, `WriteFile`, `CloseHandle`, `GetFileSize`,
`SetFilePointer`, `DeleteFileW`, `MoveFileW`, `CreateDirectoryW`,
`GetFileAttributesW`, `GetLastError`, `SetLastError`.

Paths must be ASCII-compatible and rooted in `\\fls0\\`; they are normalized
to the Hollyhock SDK's two-leading-backslash convention before calling its
file API. Transfers are synchronous, at most 4096 bytes per WinCE call, and
cross the P0/P1 boundary via 256-byte P1 bounce buffers. No WinCE ACL/share
mode semantics, handles from other processes, Unicode filenames, overlapped
I/O, full attribute metadata, or non-flash volumes. GetFileAttributesW
currently reports FILE_ATTRIBUTE_NORMAL when a stat succeeds; this does not
accurately classify directories.

### Other implemented APIs

`LocalAlloc`, `LocalFree`, `VirtualAlloc`, `VirtualFree`,
`SetKMode`, `SetProcPermissions`, `GlobalMemoryStatus`,
`NKDbgPrintfW` (limited), `OutputDebugStringW`.

**Warning:** The broad `tools/coredll_exports.txt` list contains many names
routed to `coredll_stub` returning zero. The presence of an export is **not**
evidence that it is implemented. Before porting an application, inspect
`payload/coredll.c` for the actual function and verify its semantics.

## Build and test

```sh
make clean && make -j4
# Deploy NTOS.hh3, COREDLL.DLL, WINLAB.EXE and GUIWIN.EXE together.
```

Test WINLAB touch SAVE first, inspect the toolbar status, then look for the
DAT file in the FLS0 volume. Test GUIWIN's buttons/keypad separately from
the file bridge. These changes are source-checked but still require actual
SH4 hardware verification.

## Next implementation priorities

For larger WinCE GUI ports: per-window paint regions, more than one window,
WM_CHAR generation, focus/input routing, font handling, GDI pens and
SelectObject, clipping and BitBlt, and per-thread message queues. Each should
be implemented and device-tested rather than silently mapped to a zero stub.
