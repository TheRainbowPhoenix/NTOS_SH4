# NTOS SH4 native command shell, tiling and GDI gate (22 November 2026)

This change targets the **shipping native GCC SH4BE ABI** and the small
320x528 LCD. It intentionally does **not** substitute the separate original
Microsoft WinCE MSVC ABI `payload/wince` for `COREDLL.DLL`.

## Boot and launch

The launcher looks for `CMD.EXE` when it has finished building its executable
catalog. If present, it starts CMD **once** at launcher startup, bypassing the
picker. If absent, the existing picker remains the boot UI. An exiting CMD
session returns to the original picker; this prevents an accidental infinite
autostart loop.

`CMD.EXE` registers a real WNDCLASSW, creates a real COREDLL window with
`CreateWindowExW`, handles `WM_PAINT`, `WM_KEYDOWN`, and stylus input,
and draws via `BeginPaint`, `FillRect`, and `TextOutW`.

Enter `HELP`, `DIR`, `CLS`, `RUN hello.exe`, `hello.exe`,
`RUN Apps/calc/calc.exe`, `SPLIT`, `FULL`, `SWAP`, `ROTATE`,
`KB` or `EXIT`. `RUN` is **not a process spawn**: it calls the
existing `coredll_launch` API, posts `WM_QUIT`, returns to the kernel,
and the kernel releases the old PE before mapping the next PE. After the
child returns, the loader reopens a fresh CMD session; exiting CMD itself
returns directly to the old picker. Command history is not yet persisted
across these serial handoffs. There is no parallel multi-EXE compositor
in this revision. Paths are bounded and
constrained to `\\fls0\\`; no shell pipelines, redirection,
expansion, external commands, arbitrary drive paths or `..` traversal.

## Tiles, chrome, and touch input

`payload/nt_tiling.h` is a freestanding and allocation-free layout model
using Win32 exclusive-edge rectangle geometry. Full screen is the default.
`SPLIT` creates two half-height panes. `SWAP` exchanges their positions.
`ROTATE` toggles between half-height and half-width. `FULL` maximizes
the command pane. Each pane has a compact caption/title handle and a
maximize/split touch target. The second pane is currently an internal tools
pane, **not another independent top-level HWND/process**. The layout model
is separated from the command interpreter to allow future GWES integration.

The existing native `SipShowIM` API only tracks SIP visibility. The phone
`FingerKB` is in a **different rendering/ABI profile**. CMD therefore has
a small resident, integer-only on-screen keyboard fallback for real input.
F2 toggles it, and touching the command prompt opens it. That is not yet a
system-wide WinCE SIP or text-input control.

This approach matches the *layout principles* observed in the supplied
ClassPadManagerSubscriptionForClassPadII disassembly: the panel/tab
container fits multiple independent content rectangles into a bounded
display and prefers maximizing one pane when space is constrained.
It does not reuse or copy PEG implementation code.

## Native GDI

The native `COREDLL.DLL` now clips `SetPixel`, `FillRect`, the
`Rectangle` outline, and out-of-bounds `LineTo` segments to the 320x528
display. Fully visible lines retain the fast kernel drawing gate; bounded
out-of-bounds lines use a clipped pixel raster. `Polyline` is implemented
with WinCE-compatible pen position preservation. `Ellipse` draws a thin,
symmetric pixel outline through a bounded integer scanline search, with
no FPU, multisampling, antialiasing, frame buffer copy or dynamic memory.
`GDILAB.EXE` adds six interactive pages: RGB565 fills, off-screen
clipping, line/dot grids, ellipse circles, bitmap font sizes and split
viewport geometry.

The renderer is **still not a complete Win32 GDI implementation**.
There is no region compositor, clip stack, handle-owned brush/pen state,
font selection per DC, bitmap blits, arbitrary child HWND rendering,
backing-store invalidation, or desktop window manager. Many named COREDLL
exports still fall through to an unsupported stub, and the original WinCE
`COMMCTRL.DLL` remains a separate experimental ABI compatibility profile.

## Verification and hardware gates

- `tests/nt_launch_policy_test.cpp` checks serial return-to-CMD handling,
  missing-shell fallback and the no-recursion rule on `EXIT`.
- `tests/native_cmd_tiling_test.c` checks command parsing, unsafe paths,
  split/full/side-by-side rectangle coverage, swaps, and precise hit boundaries.
- `tests/native_gdi_raster_test.c` includes the **shipping COREDLL.c**
  and checks RGB565, clipping and pixel bounds using a strict fake HAL,
  line/polyline behavior, ellipse outlines, bitmap text raster and synthetic brush lifetimes.
- `GDILAB.EXE` is a manual emulator/device test for visible artifacts and
  repaint performance, not a screenshot oracle.
- Device installer copies `CMD.EXE` and `GDILAB.EXE` with the rest of the
  native apps. No duplicate CE COREDLL is installed.
- The existing `Tests` workflow compiles the two new host tests, checks
  that both PE files exist and audits the imported symbols.

The UTLB pager remains **experimental on physical fx-CP400 hardware**.
Recheck native soft-keyboard hit areas, real LCD clipping and performance
in clemu, and verify serial app handoff and file system recovery on device
before treating the new boot path as production-ready.

For more genuine multi-process GWES behavior, the next architectural step
is a kernel-owned window-handle table with per-process owners and
screen-relative clipped DCs; tiling geometry can then be applied without
screen-sized backing surfaces. The current process teardown and single
`g_window` COREDLL contract prevent true concurrent app windows.
