# WindowsCalculator (W10M) port worklog

One big log, newest entries at the bottom. Date format YYYY-MM-DD.
Same workflow as edge/WORKLOG.md, files/WORKLOG.md and settings/WORKLOG.md.

## 2026-11-10 — session 1: blind XBF analysis + no-FPU engine + full POC (21 frames green)

### Reference artifact: Microsoft.WindowsCalculator_10.1605.1582.0_x86__8wekyb3d8bbwe.zip
The modern UWP "Calculator" (TH2 era, C++/CX XAML app — *not* the old
Win32 calc.exe we already ship): Calculator.exe 2.95 MB, VCLibs 14,
protocol `calculator:`, resources.pri carrying 19 XBF pages.
**No UI screenshots exist for this app** (unlike files/settings): the
port is blind, derived purely from the package — a deliberate test of
the uXAML component set on markup we have never seen rendered.

### In parallel: Skype triage (refs/skype-stub/)
Microsoft.SkypeApp_2.29.0.10 is the WP8.1-era **store-upgrade STUB**,
not the full client: AGHost Silverlight (`SkypeStubApp.dll`), one
MainPage (1052-line embedded XAML decoded to `refs/skype-stub/
mainpage.xaml` + app.xaml/styles.xaml), localized strings decoded.
Verdict: nothing to port now; recorded in
`refs/skype-stub/SKYPE_TRIAGE.md` — useful later as a Silverlight/
AGHost layout example.

### Analysis (calc/analysis/)
- pri decoded with the shared tooling; all 19 XBF pages carved to
  `xbf_raw/` and dumped to readable trees in `xbf_dump/`; en-US
  strings in `pri_en_us.txt`.
- Pages: App (TH2 style/theme dictionaries), MainPage + NavBar (mode
  tree: CALCULATOR Standard/Scientific/Programmer, CONVERTER 12
  categories, DATE CALCULATION), Calculator + NumberPad, the three
  operator panels (Standard 6x4, Scientific 9x5 + angle buttons,
  Programmer radix/bitflip), HistoryList, Memory, UnitConverter +
  SupplementaryResults, AboutFlyout, DateCalculator.
- `CALCULATOR_UI_REFERENCE.md`: the full port spec — page grid rows
  (expression 20 / result 72 / memory 32 / numpad 308), keypad grids,
  type ramp (SemiBold 46 px result with 12 px shrink floor), palette
  (#F2F2F2 page, #E6E6E6 numpad, accent on `=`), per-category unit
  tables, CEngineStrings error set.

### Glyphs + fonts
- `calc/app/calc_icons.c`: 25 vector operator glyphs (MDL2 E94x —
  E94A divide, E947 times, E948/E949, E94E equals, E94D negate,
  E94C percent, E94B sqrt, E94F backspace, plus composites x²/x³/xy/
  yroot/10^x/e^x, chevrons, history clock, memory book, parens, pi,
  n!, abs) in the files_icons.c 100x100 stroke model.
- 32 px and 44 px bitmap sizes baked from Liberation Sans into the
  shared `payload/phone/font_bitmap.h` (no app used anything >22 px
  before, so other apps' pick_font is unchanged). Full-ASCII coverage
  at those sizes is what makes the 46-px-class result display possible
  with the 1-bpp font stack.

### The engine: decimal int64, no FPU (SH4 constraint)
The SH4 target has no FPU — **integer only**. Value = mantissa x 10^exp
(int64): exact +,-,x,/ (alignment-based add, split-multiply keeping
16 digits), Taylor series in decimal space for sin/cos/tan/asin/acos/
atan/exp/ln, Newton sqrt with polish iterations. No double/float/libm
anywhere; the reference CEngine's rational engine is the spirit.
Engine tested standalone first (tools/regen_engine_test.py slices the
engine off calc_app.c + host harness) — all green at 16 digits.

#### Engine bugs the test caught (in find order)
1. `dec_add` exponent alignment divided in the wrong direction — one
   root cause, two symptoms: the sin hang (mod_2pi loop never
   terminated) and sqrt corruption (sqrt(2)=0.33).
2. CORDIC abandoned: Q31 cannot hold radians (pi > 2); a Q28 table
   still only gave 8 digits — replaced by Taylor in decimal space
   (full 16).
3. `dec_mul`'s 9-digit trim lost precision — split-multiply.
4. Newton sqrt polish divided by the *scaled* value instead of the
   raw mantissa; polish iterations bumped.
5. atan's Taylor was missing the alternating sign (returned ~0).
6. Digit grouping counted from the left — must count from the right.
7. An atomic-edit fail had cut two helpers (dec_abs_gt1 must precede
   dec_atan; dec_div_i misuse in div_i paths).

### The POC: calc/app (light theme, W10M-style)
- `markup/{std,sci,conv,about}.xml` -> `calc.uxaml`; `calc_app.c`
  (3245 lines: engine + converter tables + painting + input + entry).
- Modes: Standard, Scientific (DEG/RAD/GRAD), UnitConverter (12
  categories), About; hamburger nav pane (grouped mode list with
  drag-scroll), History and Memory full-surface sheets, unit dropdown
  sheet, shared SIP keyboard, SHELLCHROME navbar (Back/Home/Search).
- Component reuse (shared-DLL model): uxaml.c engine, files_icons.c
  shared MDL2 set, FINGERKB.DLL, font_bitmap.h; fingerkb.dll kept as
  a parity build like the other apps.
- e2e: `tests/calc.script` — 20 scripted shots + auto-final = 21
  frames, pixel probes in `tests/check_calc_shots.py`, plus ASCII
  display reads (tools/ascii_frame.py) to verify the *actual computed
  results*, not just paint: 12+3x4=60, sqrt(81)=9, 900+50%=1350,
  DEG sin(30)=0.5, RAD tan(pi/4)~1, 2nd+asin(0.5)=30.000000000000004,
  (2+3)x4=20, 5!=120, 2 L = 0.528344 gal (US), 100 C = 212 F (and the
  reverse F->C read 37.7778 C during verification).

#### UI/e2e bugs the frames caught (in find order)
1. Markup 16-children-per-node limit — std/sci/conv restructured with
   nested canvases.
2. Boot crash: SHELLCHROME's paint calls the hosted app's
   `render_SpGetThreadContext` export; settings/files export it, calc
   didn't. Added the render factory (tools/add_render_factory.py).
3. Inverted `!xeq` polarity, twice: in `process_tap` every command
   that wasn't "units:1" opened the unit picker; in `std_command`
   every else-if `!xeq` matched everything (digits worked via early
   return, operators/sheets never fired).
4. Stray `dyn_set("ln", 0)` in paint_sci — crash on the sci page.
5. `pick_font(38-42)` picked the '8'-only 43 px debug font (distance
   5 beat the 6 of calc32/calc44): every char became an out-of-bounds
   glyph lookup -> invisible garbage exactly on the long-result
   shrink path. pick_font now only uses full-ASCII fonts.
6. Canvas surgeries on sci.xml had dropped the parenL row and shifted
   two rows (rows must land at 193+32k); markup geometry is now
   dumped from the XML (tools/dump_markup_rects.py) instead of
   assumed.
7. Nav-pane tap-vs-scroll: host drag events coalesce (a 200 px drag
   reported 212), so scripted taps landed on dead zones (the About
   *label*). Fixed by clamping the scroll to the content height
   (deterministic row positions) and making group-header labels
   tappable.
8. First checker used wrong coordinates (written against a stale row
   map) — rewritten from the exact markup geometry.
- Host quirk for the record: this terminal eats `[m` escape sequences
  inside heredocs, which is why the session's fix scripts live as
  files under tools/.

### Next
- SH4 device build of calc.dll through the payload flow
  (build_phone.py / elf2ntx) like the other apps.
- Programmer mode (radix operators + bit-flip panel — those XBFs are
  already decoded) and Date Calculation mode.
- Memory flyout list beyond the MS sheet.
- No reference captures exist; if a real W10M device ever yields
  screenshots, recalibrate the type ramp/palette against them.

## 2026-11-11 — session 2: ship the docs + CI
- `calc/README.md`: blind-port scope, no-FPU decimal engine, e2e
  inventory (commit 5d9f1f7).
- This WORKLOG.
- `.forgejo/workflows/calc-ci.yml`: same pattern as settings-ci —
  ntos_linux smoke + calc e2e (`make -C calc/app test`) + uXAML
  round-trip; log + calc-*.png shots shipped to the `calc-ci` branch.
- Repo integrity: the 6 committed sci frames turned out to predate
  the final sci.xml row fix (a stale sheet covered the keypad on
  sci-boot..factorial); a pristine clean rebuild is deterministic
  (bit-identical across two runs, DLL differing only in PE
  TimeDateStamp + debug GUID) and passes all 21 probes — regenerated
  and recommitted. The lesson from files applies here too: always
  regenerate the full frame set from one clean run before committing.
- `calc/app/.gitignore`: ignore the generated e2e scratch
  (tests/build, tests/fs) so `git status` stays clean after
  `make test`; `calc/tools/ascii_frame.py` sys.path fixed (tools/ is
  2 levels below root, not 3).

## 2026-11-11 — session 3: capture calibration + native packages
- refs/calc-screens landed (13 real-device captures). Recalibrated the
  whole port to them: white display area (not #F2F2F2), #F7F3F7 keypad
  shade starting at the CE row, capture label rows (std y251/300/350/399/
  449, sci 7x5 at 41.5px, conv 3 cols from the XBF 0.25*/3*/0.25* grid),
  plain `=` (the captures show no idle accent — accent only on
  hover/press per the XBF), mode names in caps as the page titles
  (STANDARD/SCIENTIFIC/<category>/ABOUT), memory-row disabled greys
  (#B5B2B5 vs #636563), the nav pane 256px #E7E3E7 with no scrim and the
  #9CAEEF full-width selected band, About pinned above the nav bar.
- The launcher now owns the title bar: SHELLCHROME paints a system
  status bar (y0-24) over every hosted app, themed off the published
  NAV_BG luminance — white bg + near-black ink on light hosts, black +
  white on dark. Apps stopped painting their own top strips.
- Mirrored text fixed (the font table is LSB-first; the calc painter
  walked it MSB-first — every glyph was mirrored horizontally), 24px +
  28px fonts baked (conv values / sci result), double-strike bold.
- Sci keypad restructured to the captured 7x5: the shift(2nd) key is the
  up-arrow at R3c0; 2nd swaps R1/R2 with the InvRow sets, the new HYP
  toggle swaps trig<->hyperbolic (dec_exp-based sinh/cosh/tanh), the DEG
  button cycles units, F-E toggles a d.ddde±exp display format (fe_format),
  and Exp became the x10^ exponent entry (entry_value parses 'e'). The
  angle row is DEG/HYP/F-E at y137 per the capture.
- Converter per volume.png: 24px values left at x13, accent unit names +
  chevron, the [ - CE back ]/digits/[ - 0 . ] 3-col keypad, unit picker
  scrolling (15-unit lists), empty entry on category entry (0/0 state).
- New commands: angle:cycle, hyp, fe, mlist; units picker scroll.
- The one markup gotcha worth remembering: Button-child coordinates are
  RELATIVE to the button — the first pass placed them absolute and the
  keypad labels landed off-slot (only some rows rendered).
- e2e: 22 frames green (values verified by display reads: 60, 9, 1,350,
  -0.125, sin30=0.5, tan(pi/4)~1, asin(0.5), (2+3)x4=20, 5!=120,
  2L=0.528344gal, 3m=9.84ft, 100C=212F).
- Native testing: edge/ntos_win gained package-files/package-cortana/
  package-calc (ntos-windows-<app>.zip + run-<app>.bat, mirroring each
  app's e2e module list); files-ci and calc-ci build them and run the
  wine cross-host parity gate (same e2e script, byte-identical PNGs).
- Pending: Programmer mode (programmer*.png captures are in refs;
  needs the radix keypad + bit panel + base formatting), Date
  calculation (no capture yet), and the nav pane's Programmer/Date
  entries once those exist.

### Programmer mode (same session)
- Implemented from the four programmer*.png captures: the 6x6 radix
  keypad (Lsh/Rsh/Or/Xor/Not/And over the 2nd Mod CE C back div row and
  the A-F letter columns), base lines HEX/DEC/OCT/BIN with the active
  base in accent (tap to switch; DEC default), the big value rendered
  in the active base (28px), BIN padded to whole nibbles and grouped 4s,
  QWORD/DWORD/WORD/BYTE cycling with width masking, A-F/8/9/2-7 keys
  disabled outside their base in #B5B2B5, "." always disabled, and the
  16-nibble bit-toggle keyboard (gray 0s / accent 1s, tap flips a bit).
- Engine: int64 bitwise ops on the mantissa (And/Or/Xor/Lsh/Rsh as
  binary ops + unary Not); 2nd swaps Lsh/Rsh toward RoL/RoR (the tiny
  sub-chevron marks under the keys per the capture).
- The nav pane gained the Programmer row (3 calc modes; the converter
  band shifted +32 — the e2e coordinates had to move with it; the
  volume-section taps had silently selected the wrong rows, which is
  how a stray ProgPage BaseBin tap set the active base to BIN and
  produced a mysteriously BIN-accented prog-boot).
- Not done: Date calculation (no capture); the nav pane omits that row
  until the mode exists. The programmer pivot row omits the two
  function-page chevrons (nothing to page through yet).
