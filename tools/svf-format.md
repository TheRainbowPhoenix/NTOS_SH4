# The SVF format (`*.svf`, "Simple Vector Format")

NTOS draws its system and app icons from `VEC` command streams stored as
`.svf` files. The loader is `payload/phone/svf.c` (`SVFValidate` /
`SVFDraw` / `SVFDrawTint`); this document is the authoritative
byte-level spec for hand-editing them, and `tools/svfkit.py` is the
stdlib-only editor:

```
python3 tools/svfkit.py dump    payload/phone/assets/settings/time.svf
python3 tools/svfkit.py build   my-icon.txt my-icon.svf
python3 tools/svfkit.py preview my-icon.svf out.png --size 128 --tint 000000
python3 tools/svfkit.py kit     out.zip        # every .svf in the tree + docs
```

## File layout

```
"VEC1" | "VEC2"          4 bytes   magic (either validates; VEC2 just marks
                                  later authorship - the command set is one)
command stream           ...       see below; ends with a 0x00 terminator
```

* Whole file: 5..4096 bytes, at most 512 commands (loader limits).
* Every multi-byte value is **big-endian**.
* Coordinates are signed 16-bit, +-4096 at most (loader rejects beyond).
* Coordinates live in a **virtual viewport**, default 32x32. The `viewport`
  command rescales it (1..4096 per axis). The device maps the viewport onto
  the destination box at draw time: `screen = dest + value * box / viewport`.

## Pigments (RGB555)

Commands 0x01 carries `fill` and `stroke` as one 16-bit RGB555 word each:
5 bits red (`>>10`), 5 green (`>>5`), 5 blue, each scaled `*255/31` to the
display. `0xffff` means **transparent** (that channel of the shape is off).

Two pigments are special in the loader's `ink()`:

* `0x4210` — the "50% black" second ink, drawn `#7F7F7F` (no alpha blending
  on this display).
* any *neutral near-white* (min channel >= 22 and max-min <= 3) — when the
  app draws with a tint, these recolor to the tint. That is how
  white-authored art becomes dark ink on light tiles. Colored art, black
  outlines and `0x4210` keep their own pigment.

The top bit of the pigment word is ignored (the masks are 5-bit), so `f7cd`
is a legal pigment equal to `77cd` with bit 15 set.

## Commands

`need` is the byte count after the opcode. All coordinates/lengths
big-endian.

| op    | name        | operands (`need`)                            | draws |
|-------|-------------|----------------------------------------------|-------|
| `0x00`| END         | -                                            | stops the stream (required) |
| `0x01`| set pigments| fill u16, stroke u16 (4)                     | sets both for later shapes |
| `0x10`| rect        | l t r b i16 x4 (8)                           | fill box + 1px stroke frame |
| `0x11`| rect wide   | l t r b i16 x4, w u8 (9)                     | as 0x10, stroke frame width w (1..16, viewport-scaled) |
| `0x20`| circle      | cx cy r i16 x3 (6)                           | fill disk + stroke ring |
| `0x21`| ellipse     | l t r b i16 x4 (8)                           | bounding box -> fill + stroke |
| `0x22`| arc         | cx cy r i16 x3, start u8, count u8, w u8 (9) | stroke polyline on the circle; 32 samples per full turn (sample 0 = +x, quarter turns = 8); `count=32` is a full circle; w 1..8 |
| `0x30`| line        | x0 y0 x1 y1 i16 x4 (8)                      | stroke segment |
| `0x31`| line wide   | x0 y0 x1 y1 i16 x4, w u8 (9)                | as 0x30, width w 1..8 (parallel offsets, viewport-scaled) |
| `0x40`| polygon     | n u8, then n x (i16,i16) (1+4n), n 3..255   | fill (even-odd) + closed stroke outline |
| `0x50`| text        | x y i16 x2, len u8, len bytes (5+len)       | 12px phone font in the *stroke* pigment (max 255 bytes) |
| `0x55`| comment     | len u8, len bytes (1+len)                   | skipped by the loader (annotation) |
| `0x60`| viewport    | w h u16 x2 (4)                              | rescales the coordinate space (1..4096) |

Loader-side validation (all of it, from `scan()`):

* unknown opcode, missing END, trailing bytes after END, > 512 commands,
  file < 5 or > 4096 bytes -> the whole file is rejected (draws nothing);
* `0x22`/`0x31`: width byte must be 1..8; `0x22`: start < 32, count <= 32,
  radius >= 0; `0x40`: at least 3 points; `0x60`: 1..4096;
* coordinates outside +-4096 are clamped or rejected per command as above.

Stroke widths are *logical* (viewport units): the device scales them by
`box / viewport`, capped 1..8 (arc/line) or 1..16 (rect frames).

## The text dialect (svfkit build)

`svfkit.py dump` emits one command per line; `build` consumes the same
text, so the dump is the editable form. `#` starts a comment line.

```
viewport 32 32
set fill=ffff stroke=1d9f          # ffff = transparent
rect 1 8 28 25
rectw 1 8 28 25 2                  # frame width 2 (1..16)
circle 16 16 6
ellipse 4 10 28 22
arc 16 16 10 0 32 3                # cx cy r start count w (count 32 = circle)
line 6 24 26 8
linew 6 24 26 8 3                  # width 3 (1..8)
poly 13,1 19,1 20,6 ...           # also written as "poly 13 1 19 1 20 6"
text 8 6 "A"                       # stroke pigment, 12px font
comment hand-tuned 2026-11
end
```

Pigments are 1-4 hex digits (`1d9f`); `none` is an alias for `ffff`.
`build` runs the full device validation before writing the file, and
`dump` -> `build` round-trips every shipped asset byte-identically.

## Where the icons live

* `payload/phone/assets/settings/*.svf` — the settings tiles (system,
  battery, time, ...). Most are white fill art on dark tiles, which is
  exactly what the tint recolors.
* `edge/app/tests/assets/*.svf` — the files served from `\fls0` in the
  Edge POC (`logo.svf` is the calculator art in the markdown demo page).

Edit loop: `svfkit.py kit out.zip` packs them all at repo-relative paths
(also the CI artifact) - unzip over the repo root, dump, tweak, build,
preview with `--tint` to see the recolor, commit, push.
