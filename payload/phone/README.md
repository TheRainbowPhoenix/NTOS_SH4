# Native phone shell for NTOS SH4 BE

Seven independent, freestanding C PE32 modules, using the existing WinCE
COREDLL window/message/GDI layer. No XAMLUI dependency, FPU, desktop CRT,
heap allocation, XML parser, full TTF, photographs or external asset DLLs.
The device renderer writes the existing display through GDI; it does not
allocate a second framebuffer. Layout and app catalog are small constant
records. Six DLLs share preferred base `0x10000000` with COREDLL and carry
real SH4 big-endian literal relocations. The EXE prefers `0x00400000`.

## Build and install

In the normal toolchain environment with Python: `make phone` (also part
of `make ntx`). `make phone-check` requires the host `pefile` module.
The seven files are in `dist/`; `dist/phone-device/` contains the complete
eight-file set including COREDLL. Copy the files inside that directory to
the calculator's `fls0` root alongside your current `NTOS.hh3`, then launch
`STARTHOST.EXE` in NTOS. The existing loader resolves flat DLL filenames;
the source tree is organized by component without changing that contract.

The minimal HH3 v2.2.1 image lacks Python. On Windows, from the repo root:

```powershell
docker run --rm -v "${PWD}:/work" -w /work ghcr.io/classpaddev/hollyhock-3:v2.2.1 sh payload/phone/compile.sh
python payload/phone/build_phone.py --pack-only
python payload/phone/check_phone.py
python payload/phone/preview_phone.py
```

`preview_phone.py` uses host GCC and runs the actual C implementation
against a test-only RGB565 GDI surface. It saves PNGs under
`dist/phone-preview/`. It also exercises the whole StartHost -> UIXMobile
-> UIXn -> COREDLL message/paint lifecycle. This is a native logic/visual
test; SH4 instructions and hardware MMU execution require device testing.

## Controls

* Up/down: scroll; left: back; right: All Apps.
* Drag vertically to scroll; All apps footer opens the list.
* Tap a group letter for the alphabet jump grid.
* Tap Search, type characters; Backspace edits, left clears/back.
* Two taps or select + Enter opens an app's unavailable page. A single
  pen down selects so it can begin a drag. The current COREDLL input
  server supplies pen-down/drag, but no pen-up; this avoids launching an
  app at the start of a scroll. CLEAR exits via the existing event server.

The clock, Sunday 8 tile, mail copy and app catalog are static demo data.
Tile geometry, accent color, ordering, list spacing, icon silhouettes and
letter navigation follow the supplied 320x528 phone screenshots. News and
People use flat graphics instead of photographs. This is a lightweight
visual reconstruction, not pixel-identical live Windows Mobile output.

## API boundary and evidence

`abi/*.json` snapshots the original PE exports (names AND ordinals,
including UIXRender's base 100 and three ordinal-only exports), plus direct
and delay import names. The built six DLLs preserve exactly those export
identities. `dist/phone-build.json` lists implemented and stubbed exports
per module; unsupported exports return `E_NOTIMPL` rather than success.
Those generic stubs have no out-parameter contract and must not be treated
as working APIs. Do not call them expecting an initialized object.

Ghidra project `/recreate/WinPhone/10`:

* `starthost.exe` at `00401605`: loads StartMenu, calls
  StartMenuInitialize, passes `res://StartMenu!Main.uix#Frame` and `Home`
  to RunApplication, then calls StartMenuUninitialize.
* `uixmobile.dll` at `1002ecf0`: initializes UIX, runs the application,
  shuts it down. Its RunApplication is export ordinal 15.
* `startmenu.dll` at `10027a70`: StartMenuInitialize.

The SH4 StartHost imports those same three application methods by their
real names. StartMenu is an explicit PE dependency instead of a dynamic
LoadLibrary/GetProcAddress lookup. Desktop CRT/SEH/ETW startup and the
API-set DLL imports from the x86 host are deliberately omitted. Mirroring
every transitive original import would exceed this tiny profile and the
loader's 16-module limit; full import parity is not implemented.

The internal rendering/context and schema-factory tables in `phone.h`
are a **private reduced profile**, marked `PROFILE`, not Microsoft's COM,
Sp engine ABI or binary UIB format. UIXLoadMarkup recognizes only the
Start root URI and resolves it to compiled records; it rejects other
markup. UIXInitializeApplication accepts the reduced App descriptor.
Several simple state/resolution APIs work; the majority of renderer and
shell service exports remain stubs. Existing x86/ARM phone binaries cannot
run on SH4, and a recompiled application needs adapters for its actual API
usage. Export parity is a foothold for that work, not binary compatibility.

WinCE is the platform boundary: RegisterClassW/CreateWindowExW,
GetMessageW/TranslateMessage/DispatchMessageW, BeginPaint/EndPaint,
CreateSolidBrush/FillRect/DeleteObject. Rendering and interaction are
implemented independently; no Microsoft CE/NT source was copied.

The checked-in small 1-bpp font header is baked from installed Windows
Segoe UI glyphs at a few fixed sizes. It has no runtime dependency on
Windows fonts or Pillow. `bake_font.py` is an optional authoring tool to
regenerate it locally; it needs Pillow and those fonts installed.

## Checks

`check_phone.py` independently parses the final PE files with pefile,
compares exact export identity sets against the references, resolves every
import across the eight shipping modules, checks machine/subsystem,
applies a BE relocation delta to every literal site and enforces a 256 KiB
aggregate mapped-image budget. Native C tests check RGB565 tile colors,
gutters, clipping, scrolling boundaries, list dragging, search, alphabet
jumping, reinitialization and the WinCE application lifecycle.
