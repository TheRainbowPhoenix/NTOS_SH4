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
