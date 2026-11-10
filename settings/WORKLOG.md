# SystemSettings (W10M) port worklog

One big log, newest entries at the bottom. Date format YYYY-MM-DD.
Same workflow as edge/WORKLOG.md and files/WORKLOG.md.

## 2026-11-10 — session 1: XBF analysis + full POC (all reference screens green)

### Reference artifact: Microsoft.Windows.SystemSettings_cw5n1h2txyewy.zip
W10M inbox Settings (10.0.14393.0, RS1): SystemSettings.Phone.exe 2.3MB
(native WinRT/CX — the CLR data dir is empty, so no .NET metadata to
mine), SystemSettingsViewModel.Phone.dll 455KB, resources.pri 9.3MB
(2359 resources, 2355 en-US values).

### Analysis (settings/analysis/)
- `pri_decode.py` (shared tool): 186 XBFs carve out with
  pri_extract_files.py — VMInstance/*.xbf (~110 page view-models:
  SettingsPageEntry -> SettingsGroupEntry -> SettingEntry by resource
  UID), View/*.xbf (RootPage = root Frame + dialogFlowFrame +
  SettingsFlyout; L1Page = landing; L2Page = 320px master/detail nav;
  Styles 45KB; Templates with Boolean/NavigateToPage/ComboBox/Slider
  setting templates; SettingsFlyout/SettingsSearchBox/...).
- Page registry strings in the exe: SettingsPage<TypeName> <->
  ms-settings:<uri> (77 URIs); L1 categories from the pin-tile table
  (SettingsPageGroupPCSystem..Extensibility + wide-tile PNGs) with
  display strings in the pri ("System"/"Display, notifications,
  battery", "Network & wireless"/"Wi-Fi, airplane mode, cellular"...).
- Per-page glyphs are assigned in native code (VM dll has 22 single
  PUA string constants, no static table; no rizin in this env) — the
  port uses the glyph SHAPES read off the reference captures instead.
- `SYSTEMSETTINGS_UI_REFERENCE.md`: full spec — L1Page/L2Page/Templates
  structure + every refs/settings-screens capture measured (header
  gear+"Settings" y24..71, 2px accent line, focused search box, 64px
  category rows, W10M toggle 34x20 left-of-label, slider 2px track +
  8x22 pill thumb, combobox 1px #9C9A9C, 20px checkboxes, 6-col
  swatches, About value rows, Active-hours 3-cell pickers + the hour
  picker overlay with the #ADB6F7 selected band, the Edit-device-name
  FLOW page with the SIP, and the palette: #F7F3F7/#3965FF/#636163/
  #CECFCE/#9C9A9C).

### The POC: settings/app (light theme, like the W10M Settings app)
- markup/*.xml -> settings.uxaml (HomePage, CategoryPage, DeepPage,
  SearchPage, EditNamePage) + settings_app.c painting the live chrome
  and controls over the anchors, exactly the files_app.c model:
  - Home: gear+"Settings" header, 2px accent line, "Find a setting"
    focused box, 10 category rows (accent stroke icons + bold name +
    gray description) with drag-scroll + auto scrollbar.
  - L2 pages per category (subcategories from the page registry) with
    the gray category heading.
  - Deep dives: Display (2 sliders + 2 toggles + Apply/Cancel),
    Colors (Dark/Light radios + 24-swatch 6-col grid with selection
    ring), Sounds (ringtone combobox + link + Vibrate toggle + 3
    checkboxes), About (value rows + Edit name + More info), USB
    (2 toggles), Start (live-tile preview mock + background flyout),
    Active hours (2 paragraphs + 2 time boxes + Save/Cancel + the
    hour picker with 3 columns and the light-blue selected band).
  - Edit device name flow page: 2px accent field + caret + Save/
    Cancel + the shared SIP (docked 40 above the navbar).
  - Search: query field + X, autosuggest strip, substring-matched
    results (the "te" query finds "Date & time"/"Phone update"),
    result rows open their page.
  - SHELLCHROME navbar (Back/Home/Search wired: back walks the
    surface stack, home resets, search opens the query flow).
- settings_icons.c: 22 new stroke glyphs (gear, phone, keyboard+mouse,
  globe, brush, person, clock+chat, accessibility, padlock, update
  arrows, puzzle, tiles, palette, note, screenlock, navbar, monitor,
  battery, storage, info, play, usb) in the files_icons.c 1px style.
- e2e: 28-frame script through every reference screen state incl.
  drag-scroll, swatch pick, toggles, sliders, flyout shard (scrim
  over the title bar — the files lesson), keyboard typing, picker;
  check_settings_shots.py verifies 26 screens with windowed color
  probes (window matching because 1px font strokes never fill a
  whole pixel).
- .forgejo/workflows/settings-ci.yml: same pattern as files-ci
  (ntos_linux smoke + settings e2e + uxaml round-trip; log + shots
  shipped to the settings-ci branch).

### Notable bugs found by the e2e
1. Drag scrolls fired the WM_DOWN_ tap (rows opened on scroll start):
   taps now activate on WM_UP_ only when the press never became a
   drag — proper tap-vs-pan disambiguation.
2. disp_y_auto used a literal 0 instead of the wrap line count, so
   the auto-brightness toggle band was 44px too high — the scripted
   tap missed it.
3. Drag totals are interpolation-dependent (never exactly the finger
   distance) — the e2e now scrolls with a clamped max drag so row
   positions are deterministic.

### Next
- Real-device captures to refine the glyph shapes + row pitch.
- The remaining reference screens (settings_update_active_hours
  hourpicker scroll states, edit_device_name focus caret blink).
- SH4 device build of settings.dll through build_phone.py / elf2ntx.
