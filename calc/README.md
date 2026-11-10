# WindowsCalculator (W10M) port

The modern UWP "Calculator" (Microsoft.WindowsCalculator 10.1605.1582.0,
TH2 — *not* the old Win32 `calc.exe`) reconstructed for NTOS. No
reference screenshots exist for this app, so the whole UI is derived
blind from the package: the 19 XBF pages carved out of `resources.pri`,
the en-US string table and the TH2 style dictionaries — the port
doubles as a test of the uXAML component set on unseen markup.

- `refs/calc-screens/` — real-device captures (added after the blind
  port): the whole layout is now calibrated to them — white display area,
  #F7F3F7 keypad shade from the CE row, capture label rows, plain `=`
  (no idle accent), mode names in caps as titles, the nav pane 256px
  #E7E3E7 with the #9CAEEF selected band.
- `analysis/` — pri/XBF dumps + `CALCULATOR_UI_REFERENCE.md` (the port
  spec: mode tree, keypad grids, type ramp, palette, unit tables).
- `app/` — the POC: uXAML markup (`markup/*.xml` -> `calc.uxaml`) +
  `calc_app.c` + `calc_icons.c`, built on the shared-DLL model like the
  other apps (uxaml.c engine, files_icons MDL2 set, FINGERKB.DLL
  keyboard, SHELLCHROME.DLL navbar). Modes: Standard, Scientific
  (DEG/RAD/GRAD), UnitConverter (12 categories), About; hamburger nav
  pane, History/Memory full-surface sheets, unit dropdown sheet.
- Arithmetic: **the SH4 target has no FPU** — the engine is a decimal
  int64 type (value = mantissa x 10^exp): exact +,-,x,/ and
  series/Newton transcendentals (Taylor trig/atan/exp/ln, Newton sqrt),
  no `double`/`float`/libm anywhere; 16-digit precision verified by an
  engine test before the UI was wired.
- `make -C calc/app test` runs the scripted e2e under NTOS_SH4_linux
  and verifies 21 screenshots with pixel probes; the scripted paths
  compute real results (12+3x4=60, sqrt(81)=9, 900+50%=1350, DEG
  sin(30)=0.5, RAD tan(pi/4)=1, asin(0.5)=30deg, (2+3)x4=20, 5!=120,
  2 L = 0.528344 gal, 100 C = 212 F).
- `tools/` — session scripts (font baking, XBF/engine surgery, markup
  rect dump, ascii frame reader).
- CI: `.forgejo/workflows/calc-ci.yml` (log + shots on the `calc-ci`
  branch) — it also builds a **native test package** the same way edge
  does: `edge/ntos_win` `package-calc` assembles `ntos-windows-calc.zip`
  (interactive LCD + session recorder) and the wine gate proves the
  windows host renders byte-identical PNGs for the same `calc.script`.
