# SVF VEC2 authoring and NTOS Settings XAML look

## Compatibility and wire format

VEC1 remains accepted unchanged, as do the original SVF.DLL exports
`SVFValidate` and `SVFDraw`. The optional `SVFDrawTint` export used by
the light shell takes an additional RGB888 tint. VEC2 shares the same
4-byte magic/header, big-endian signed 16-bit coordinate ordering,
uint16 RGB555 color fields and the 0x60 viewbox metadata.

| Opcode | Payload after 1-byte opcode | Function |
| --- | --- | --- |
| 0x01 | fill:u16, stroke:u16 | RGB555/transparent state |
| 0x10 | left,top,right,bottom:i16 | filled rectangle |
| 0x11 | same + width:u8 | bordered rectangle |
| 0x20 | cx,cy,r:i16 | circle |
| 0x21 | left,top,right,bottom:i16 | ellipse |
| **0x22** | cx,cy,r:i16, start:u8, count:u8, width:u8 | sampled arc |
| 0x30 | x0,y0,x1,y1:i16 | 1-pixel line |
| **0x31** | x0,y0,x1,y1:i16, width:u8 | thick line |
| 0x40 | vertex-count:u8 + (x,y:i16) × count | polygon |
| 0x50 | x,y:i16 + text-length:u8 + UTF-8 bytes | text |
| 0x55 | length:u8 + arbitrary UTF-8 bytes | object metadata |
| 0x60 | width,height:u16 | viewbox |
| 0x00 | none | end |

An arc's start is **0–31**, clockwise samples relative to +X.
`count` is **0–32** in 1/32 turns (8 = 90°, 32 = full circle).
Thickness is a logical 1–8 units before scaling. The on-device
renderer uses 32 precomputed integer cosine/sine samples and straight
line segments; there are no bezier evaluations, floating-point
instructions or dynamic allocations in the SVF decoder.

VEC2 parser rejects a truncated command, invalid arc angles/count,
out-of-range radii, zero/oversized widths and unknown commands.
Old VEC1 and VEC2 commands may be mixed. `0xffff` is transparent.

## The two ink tones

For light UI surfaces, `SVFDrawTint` interprets two deliberately
reserved neutral pigments:

* `0x7fff`: primary solid black (RGB 0,0,0 when tint is black).
* `0x4210`: second solid 50%-black (RGB 127,127,127).
* `0xffff`: transparent, not a visible ink.
* Other RGB555 colors: retain their original RGB components.

The original `SVFDraw` does not recolor either tone and retains the
original color ABI. The new generator uses the VEC2 header for
extended Settings/Cortana assets. Settings icons use the same stroke
thickness throughout to avoid mixed visual weights.

## Design targets and screenshot preflight

The intended output sizes are **17px** for Settings subcategories,
**24px** for the header, **32px** for Settings categories, and
**27px/36px** for Start tile glyphs. These are output pixel sizes,
not arbitrarily large SVG intrinsic dimensions.

User-supplied Microsoft MDL2 SVG shapes were included as *reference*
assets for System/Devices/Edge under
`payload/phone/svg_reference/`. The build uses newly authored
integer SVF approximations, not an SVG runtime.

On a development PC with Pillow and CairoSVG:

```sh
python3 -m pip install Pillow CairoSVG
make -j4
python3 tools/svf_icon_preflight.py --all --root dist
```

This creates three per-size visual comparisons and reports normalized
RGB error at 17/24/32px. The tool is a **diagnostic**, not a promise of
exact pixel identity: reference SVG anti-aliasing, the coarse 32-step
integer arc and the different UIX stroke rasterization are expected to
give nonzero error. The SH4 decoder uses no Cairo or Python code.

The user-provided Python `svf.py`, `svg_converter.py` and
`svf_editor.py` informed the on-device command set/editor design.
The supplied SVG parser approximated cubic curves offline, and the
native C implementation intentionally does **not** need that Python
stack, `gint`, `cgui`, or a Python runtime.

## New on-device editor

`SVFEDITOR.EXE` is a **standalone SH4 big-endian WinCE PE**, imported
from the **single shared COREDLL.DLL and SVF.DLL**. Open via the NTOS
File Explorer. Toolbar: Rect / Line / Circle / Arc; toggle primary or
50%-black ink; touch-drag on the 32×32 canvas; Undo (new additions),
Clear, Save, Load. The file is written as
`\\fls0\\ICON.SVF` by the existing COREDLL
`CreateFileW/WriteFile/ReadFile/CloseHandle` methods, outside any
exception handler. Loaded files are validated by the runtime SVF
decoder. Loaded layers are preserved and new strokes can be appended;
per-object selection and editing of imported geometry is **not yet
implemented**.

`make -j4` includes the editor in the ordinary `dist/`; the device
packager copies `SVFEDITOR.EXE` into the *same* `NTOS-CP400` ZIP.
There is no alternative DLL set and no second release artifact.

## Settings state (non-hardware preview)

The UWP-inspired Settings page has consistent pressed-row shading,
a flat pushed-button state without a bottom underline, a 100/125/150/175%
scale dropdown, global search suggestions from any level, and System →
About. About names the CP400/SH4AL-DSP, 320×528 display and *experimental*
NTOS API layer rather than copying the reference desktop's Windows 10
build, RAM, CPU or device ID. Brightness, scaling, encryption, and update
controls remain **UI previews**; they do not change hardware settings.

## Runtime caveats

These paths are validated by SH4 compilation, native SVF decoder
contracts, UI host tests and package import checks. Actual graphical
appearance, touch-release handling, file writing and interaction latency
on the real CP400 are only established by subsequent emulator/device
testing, not by CI.
