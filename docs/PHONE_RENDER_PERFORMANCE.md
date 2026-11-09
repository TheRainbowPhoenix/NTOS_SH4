# Light UI and renderer performance — 9 November 2026

## Fixes and compatibility

* Legacy VEC/SVF icon assets are predominantly monochrome white for dark
  phone tiles. A **new opt-in** `SVFDrawTint` export maps near-white neutral
  pigments to dark ink, without changing `SVFDraw`, colored glyphs or
  the original public ordinal assignments. `CatalogDrawIconTint` similarly
  retains `CatalogDrawIcon` and is used by Start tiles, app lists, and
  app headers. Private new exports have appended ordinals (SVF #3,
  APPCATALOG #5); ordinals #1–2 and #1–4 respectively remain untouched.
* `FINGERKB.DLL` uses the same tint path for shift/caps/delete/search
  icons. Inactive Quick Settings icons use slate ink, active tiles use
  white, matching the accompanying text.
* The phone navigation bar and COREDLL's native WinCE bar both use the
  same light panel and near-black Back/Start/Search glyphs. The search
  magnifier is a circle with a lower-left handle, not a hexagon.
* COREDLL draws a single pale title bar with dark caption and flat close X;
  CALC.EXE no longer draws a second title bar over it.
* App tiles switch to soft category pastels so their dark SVF icons
  remain legible under light mode; native PE raster icons retain their
  original multicolor pixels.

## Real bottlenecks seen in source

A full 320×528 screen contains **168,960 pixels**. Previously, an
expanded notification shade drew the *entire* menu and sometimes
keyboard underneath the opaque shade, redrawing the same screen
multiple times. All SVF text and vector shapes are rasterized via
`FillRect` calls into the OS framebuffer; WinCE's synthetic brush
creation and per-pixel or per-glyph rectangles add substantial
cross-DLL overhead. The legacy COREDLL window server also queued
`WM_PAINT` *and* performed synchronous `UpdateWindow`, leaving a
stale paint for the next event and an extra `LCD_Refresh` call.

Changes: one coalesced invalidation, exactly one refresh per
synchronous `EndPaint`, skip fully occluded surfaces under the
expanded shade, reuse one synthetic brush per color, and batch
same-color horizontal PE-icon pixel runs. Horizontal and vertical
SVF strokes map to a single rectangle. Power-of-two coordinate
scaling avoids the 32-round software divide common in VEC1 32×32
viewports.

## Quantitative repeatable host measurements

`python3 payload/phone/preview_phone.py` now records every scene
in `dist/phone-preview/draw-profile.csv`, with number of GDI
rectangles, total touched pixels (including overdraw), and host
CPU milliseconds for the drawing pass (not image encoding).
The *same* renderer C sources are used by the preview and SH4 PE
DLLs. Values below were captured from GitHub Actions runs
`42680900087` (baseline) and `42680957778` (occlusion fix),
both passing C/C++ CI:

| Metric, notifications.ppm | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| GDI FillRect calls | 3,916 | 2,463 | 37.1% |
| Pixels touched, including overdraw | 445,688 | 256,841 | 42.4% |
| Pixel-overdraw ratio vs one full screen | 2.64× | 1.52× | — |

Other prominent pre-optimization scenes: Excel project 3,042
FillRect calls, Settings main 2,816, and Home 2,505.
The host CPU timings are affected by runner load and should not
be interpreted as device FPS. The host preview now enforces
`notifications*.ppm` under 2,800 rectangle calls and 300,000
painted pixels to prevent accidental rediscovery of this bug.

## Next bottlenecks worth measuring on real hardware

1. Time `LCD_Refresh` separately from CPU-side draw using host-safe
   timestamps outside any exception handler.
2. Cache static Start icons/letters or entire backbuffers with
   dirty-region invalidation, rather than recreating 2,500+
   rectangles on each key/touch event. This needs an explicit
   memory budget: no extra full-screen buffer is allocated today.
3. The remaining diagonal line rasterizer and glyph scanline loop
   still issue numerous tiny rectangles. A framebuffer-aware
   batched scanline path would reduce API crossings but must
   coordinate with the Casio LCD and its cache coherency.
4. Avoid synchronous file scanning or CPL discovery in paint;
   Control Panel already shows its loading popup first.

The rendering changes are host-preview and CI tested. Their actual
perceived performance on the CP400 emulator and physical hardware
still requires runtime comparison; no numeric on-device speedup
is claimed.
