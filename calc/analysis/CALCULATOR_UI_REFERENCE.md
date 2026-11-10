# WindowsCalculator 10.1605.1582.0 (W10M) — UI reference for the NTOS port

Derived **entirely from the package** (no reference screenshots exist for this
app): all 19 XBF pages decoded from `resources.pri` (`xbf_dump/`), the en-US
string table (`pri_en_us.txt`), and the TH2 `App.xbf` style/theme dictionaries.
This is deliberate: it doubles as a blind test of the uXAML component set.

Reference package: `refs/edge/Microsoft.WindowsCalculator_10.1605.1582.0_x86__8wekyb3d8bbwe.zip`
(C++/CX XAML app, VCLibs 14, `Calculator.exe` 2.95 MB, protocol `calculator:`).

## Modes & shell (MainPage.xbf + NavBar.xbf)

- 32 px title bar, `AppNameTitle` = "Calculator", 12 px, left margin 12
  (transparent bg; on our light build it sits on the page background)
- `NavButton` hamburger (`\ue700`, HamburgerStyle: ChromeMediumLow bg, 48x48,
  Bold 20) opens the `NavBar` SplitView pane: a grouped list
  - **CALCULATOR** — Standard, Scientific, Programmer
  - **CONVERTER** — Volume, Length, Weight and Mass, Temperature, Energy,
    Area, Speed, Time, Power, Data, Pressure, Angle
  - **DATE CALCULATION** — (one entry)
  - rows: MinHeight 48 (32 on tiny), text at x=48, Body style; group headers
    Bold, CharacterSpacing 75, MinHeight 32/48
- Mode hosts: `CalcHolder` (Standard/Scientific/Programmer), `ConverterHolder`,
  `DateCalcHolder`; on phone only one is live at a time
- Page bg `SystemControlBackgroundAltHighBrush` = **#F2F2F2 light**

## Calculator page (Calculator.xbf) — shared by Std/Sci/Prog

Grid rows (phone, effective px): `RowHamburger` 48, `RowExpression` 20*
(min 20), `RowResult` 72* (min 72), `RowDisplayControls` 0 (Std),
`RowMemoryControls` 32* (min 32), `RowNumPad` 308*.

- `HistoryButton` top-right 48x48: clock-with-arrow **path** icon, opens the
  History flyout (`Placement=Full`, bottom sheet)
- `expressionText` (OverflowTextBlock, row 1): the running expression, small
  gray, right-aligned, bottom-anchored, margin 6,0
- `Results` (`CalculationResult`, row 2): right-aligned, **SemiBold 46 px**
  (`CalcResultSnappedFontSize`), auto-shrink floor 12 px, chevrons
  `\ue26c`/`\ue26b` when clipped; "Display is " a11y prefix
- `MemoryPanel` (row 4): 6 equal cells (max 80 px each) — **MC MR M+ M- MS**
  (CaptionButtonStyle: 12 px, BaseMedium fg, transparent bg, ListLow hover,
  ListMedium press) and **M** toggle (path icon) opening the Memory flyout
- `NumpadPanel` (row 5): the mode's operator panel + NumberPad, on a
  `ChromeLow` shade (#E6E6E6 light) with a 1 px top hairline

### Standard mode (CalculatorStandardOperators.xbf + NumberPad.xbf)

6 rows x 4 columns (NumberPad = 3 cols x 4 rows, negate from the parent
panel fills its bottom-left cell):

| row | col0 | col1 | col2 | col3 |
| --- | --- | --- | --- | --- |
| 0 | **%** `\ue94c` | **√x** `\ue94b` | **x²** `𝑥²` | **1/x** `¹⁄𝑥` |
| 1 | **CE** | **C** | **⌫** `\ue94f` | **÷** `\ue94a` |
| 2 | **7** | **8** | **9** | **×** `\ue947` |
| 3 | **4** | **5** | **6** | **−** `\ue949` |
| 4 | **1** | **2** | **3** | **+** `\ue948` |
| 5 | **±** `\ue94d` | **0** | **.** | **=** `\ue94e` |

Styles: numbers `NumericButtonStyle24` (CalcButtonStyle base: transparent,
BaseHigh fg, **SemiBold 24**), operators `OperatorButtonStyle` (MDL2 glyph,
SemiBold **16**), CE/C `ClearOperatorStyle` (15 px), x²/1/x `InvertOperatorStyle`
(Segoe UI Symbol, 16), `=` `EqualOperatorStyle` (**accent #0078D7-class bg on
hover/press, white fg** — the one colored button; on our W10M device accent is
#3965FF like the other ports). Press feedback: ListLow/ListMedium tints.
Engine strings (CEngineStrings): ÷ × + - = . ( ) % Mod sin cos tan ... and
errors "Invalid input" / "Result is undefined" / "Overflow" /
"Divide by zero" (`\CEngineStrings\...`).

### Scientific mode (CalculatorScientificOperators.xbf + AngleButtons)

`RowDisplayControls` becomes 32 px holding the angle buttons
(`CalculatorScientificAngleButtons`: **DEG / RAD / GRAD** toggles);
`RowNumPad` 276*; a hairline separates display from keypad. Operator panel
(2nd state swaps primary row): x², xʸ, ʸ√x | eˣ, 10ˣ, 1/x, |x| | sin, cos,
tan + 2nd/hyp variants, sec/csc/cot | ln, log₁₀, e, n! | π, mod, exp,
parens ( ) — MDL2 glyphs `\ue940`-`\ue95x` + Segoe UI Symbol superscripts.

### Programmer mode (RadixOperators + BitFlip + DisplayPanel)

HEX/DEC/OCT/BIT radix selector row, QWORD/DWORD/WORD/BYTE, the bit-flip
grid (64 little squares) and A-F letter row. (Stretch goal for the port.)

### Date calculation (DateCalculator.xbf)

Mode toggle "Difference between dates" / "Add or subtract days", From/To
date pickers, result line + unit breakdown (day/week/month/year words all
in pri_en_us). (Stretch goal.)

## UnitConverter (UnitConverter.xbf)

Rows: top nav 48 | `Value1` 56* (right-aligned, ValueMediumStyle) |
`Units1` combo 32* | `Value2` 56* | `Units2` combo 32* |
SupplementaryResults 48* | numpad 272* on the ChromeLow shade.
- value displays: same CalculationResult control, active value = full black
  SemiBold, inactive = gray; tapping a display switches which side you type
  into (Value1Active/Value2Active)
- unit combos (`ComboStyle`): full-width, 12 px margins, the units of the
  category (all 153 unit names + abbreviations in pri_en_us)
- SupplementaryResults: "Also equal to" caption + horizontal list of
  whisker-equal values (the other same-family units), BaseMedium
- numpad: CE + ⌫ (`\ue750`) top row over the pad, ± / 0-9 / . below
  (`NumericButtonStyle28` = 28 px), no operators
- 12 categories: Volume, Length, Weight and Mass, Temperature, Energy, Area,
  Speed, Time, Power, Data, Pressure, Angle (conversion factors are in the
  C++ binary; the port uses standard SI constants)

## History / Memory flyouts (HistoryList.xbf / Memory.xbf)

`Placement=Full` bottom sheets over everything (same layer rule as the files
dialogs): ChromeMediumLow sheet (#DCDCDC light) with 1 px top hairline, list
of items (expression: Caption SemiBold gray right-aligned; result: Base
right-aligned), empty-state text "There's no history yet" /
"There's nothing saved in memory", trash (`\ue74d`) clear button bottom-right
(History) / "MC" (Memory), memory rows carry per-row MC / M+ / M- accent
buttons. Enter on a history row re-runs it.

## About flyout (AboutFlyout.xbf)

Hamburger slot button "SETTINGS", "About" Subtitle title, body: "Calculator"
/ "Version 10.1605.1606.06002" / "Copyright 2016 Microsoft Corporation. All
rights reserved." / Terms of Use + Privacy Statement hyperlinks, and a
"Send feedback" button.

## Palette (light, from App.xbf ThemeDictionaries[Light])

| token | value | use |
| --- | --- | --- |
| AltHighColor | #F2F2F2 | page/display background |
| ChromeLowColor | #E6E6E6 | numpad shade |
| ChromeMediumLowColor | #DCDCDC | hamburger bg, flyout sheets |
| BaseHigh (text) | #000000 | primary text |
| BaseMedium (text) | #5D5A5C-class | captions, memory buttons |
| Accent | #0078D7-class (device: #3965FF) | = button, links, selection |
| ListLow / ListMedium | #F0F0F0 / #DEDBDE-class | hover / press tints |
| hairline | #E0E0E0-class | shade top edge |

Type ramp: result 46 SemiBold (floor 12), numbers 24 SemiBold, operators 16,
CE/C 15, memory 12, expression ~15 BaseMedium, title 12.

## Port plan (POC, uXAML)

1. `calc/app/markup/*.xml` — pages as uXAML: `StdPage`, `SciPage`,
   `ConvPage`, `AboutPage`, `NavPane` (the SplitView pane content)
2. `calc/app/calc_app.c` — shell (title bar + hamburger + mode switch),
   standard engine (immediate-execution, doubles, CEngine string table),
   scientific engine (trig/hyp/inv, angle modes, parens, factorials),
   converter (12 categories, factor tables, supplementary results),
   history/memory stores + full-sheet flyouts, About page
3. shared components: FINGERKB.DLL not needed for Std (numeric-only SIP
   later), SHELLCHROME.DLL bottom strip like files/settings
4. e2e under `edge/ntos_linux` with shot checks (layout + palette anchors
   above), `calc-ci` workflow shipping screenshots
