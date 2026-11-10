# SystemSettings (W10M 10.0.14393) — UI reference for the NTOS port

Source: `refs/edge/Microsoft.Windows.SystemSettings_cw5n1h2txyewy.zip`
(SystemSettings.Phone.exe 2.3MB native WinRT/CX + SystemSettingsViewModel.Phone.dll
455KB + resources.pri 9.3MB) and the real-device captures in
`refs/settings-screens/` (light theme, 320x528).

## Package inventory

- `resources.pri`: 2359 resources — 186 XBFs + en-US strings (2355 values).
  Decoded with the shared `files/tools/pri_decode.py` (`pri_en_us.txt`,
  `pri_resources.txt`), extracted with `pri_extract_files.py`.
- XBF classes (all decode with `files/tools/xbf_dump.py`):
  - `app.xbf` (near-empty Application), `View/RootPage.xbf` = root Frame +
    dialogFlowFrame + SettingsFlyout (the shard host).
  - `View/L1Page.xbf` — landing: NavBarBrush bg, centered "Settings"
    HomeTitle (margin 0,23,0,15) + SettingsSearchBox (margin 16,12, w336) +
    SettingsNavGridView (ItemsWrapGrid, NormalPageGroupItemTemplate:
    centered FontIcon 48px AccentTextBrush + DisplayName + Description).
    Desktop shape — the PHONE reference (settings_home.png) shows a LIST:
    40px glyph column + bold name + gray description, ~64px pitch.
  - `View/L2Page.xbf` — master/detail: 320px nav pane (HomeButton \ue115
    48px row, search row, category title, SettingsNavListView — item =
    8x24 selection mark + 40px FontIcon column + DisplayName) and a content
    SettingsPageHost on the right; phone shows the list, deep-dive replaces it.
  - `View/Styles.xbf` (45KB, 3886-line dump) + `View/Templates.xbf` +
    `View/Globals.xbf`: the full style library. Key styles:
    NavBarListViewItemStyle (MinHeight 48, ListViewItemPresenter, selection
    mark 8x24 @ -4 margin), NavBarItemTextBlockStyle (StandardText ramp,
    LineHeight 20), AppTitleTextBlockStyle (TitleText ramp, MaxLines 2),
    GroupTitleTextBlockStyle, ToggleSwitchStyle, CheckBoxStyle,
    ComboBoxStyle, SliderStyle, RadioButtonStyle, DialogButtonStyle.
  - `VMInstance/*.xbf` (~110): SettingsPageEntry objects — groups ->
    setting entries by resource UID (the actual page contents).
  - `View/*PageTemplates.xbf` (~60): per-page control trees.
- Page registry (strings in the exe): SettingsPage<TypeName> <->
  ms-settings:<uri> (about, backup, batterysaver, colors, datausage,
  dateandtime, easeofaccess-*, emailandaccounts, findmydevice, lockscreen,
  maps, network-*, notifications, personalization-*, privacy-* (20 pages),
  speech, storagesense, sync, usb, windowsupdate*, yourinfo, display,
  messaging, mousetouchpad, phone, provisioning, screenrotation,
  signinoptions, sounds, developers, deviceencryption, nfctransactions...).
- L1 categories (exe pin-table, `SettingsPageGroup*` UIDs + wide-tile PNGs):
  PCSystem (System), Devices, Network ("Network & wireless"),
  Personalization, Accounts, TimeRegion ("Time & language"),
  EaseOfAccess ("Ease of Access"), Privacy, Restore ("Update & security"),
  Extensibility ("Extras"). Display strings in `pri_en_us.txt`
  (SettingsPageGroupPCSystem\DisplayName "System" + Description
  "Display, notifications, battery" etc).
- Per-category glyphs are set in native code (VM dll string heap has 22
  single-glyph constants; no static table) — the port uses the reference
  captures' glyph shapes (VLM-read, listed below) instead.

## Reference captures (refs/settings-screens) — measured geometry

All pages: status bar y0..23 (host: none), app header y24..71 (48px):
blue gear glyph x8..40 + bold title x48 (20px). Host mapping = ref y - 24.

### settings_home.png (L1) — the PHONE shape
- Header gear+"Settings" on #F7F3F7; 2px accent #3965FF line y80..81
  (host 56..57); search box y82..106 (host 58..82): white, 2px accent
  border (focused), placeholder "Find a setting" gray, magnifier right.
- Category rows, ~64px pitch, first icon y135 (host 111): glyph 32px in
  x12..44 accent blue; name bold ~15px x56 (host row_y+2); description
  13px gray #636163 x56 (row_y+22).
- VLM-read glyphs: System=phone outline, Devices=keyboard+mouse,
  Network=globe, Personalization=brush over display, Accounts=person,
  Time & language=clock+speech bubble (Ease of Access=accessibility person,
  Privacy=padlock, Update & security=circular arrows, Extras=puzzle —
  from platform knowledge, off-screen in the capture).

### personalization.png (L2)
- Same header+accent+search; section heading "Personalization" gray;
  subcategory rows: glyph 24px x12..44 + label (no description):
  Start (tile grid), Colors (palette), Sounds (note), Lock screen
  (screen+padlock), Navigation bar (rounded rect).

### display.png (deep dive)
- Header title "Display"; white content from y72 (host 48).
- "Adjust brightness level" label + slider: blue-filled track from left,
  gray rest, thumb = vertical pill 8x22 blue.
- "Change brightness automatically when lighting changes" (2 lines) +
  toggle BELOW the label, left-aligned: pill x12..46 (34x20), ON = solid
  #3965FF + white knob right + "On" text x52; OFF = #CECFCE + knob left.
- "Rotation lock" + toggle OFF.
- "Size of text, apps, and items on this display" (2 lines) + slider (full).
- Apply / Cancel: gray #CECFCE buttons ~90x30 left row (x12, x124).

### colors.png
- "Choose your mode" heading + radios Dark/Light (Light selected).
- "Choose your accent color" + 6-column swatch grid (40px cells).

### sounds.png
- "Ringtone" heading; combobox x12..308 h34: 1px #9C9A9C border, white bg,
  play glyph, value "Pure", chevron-down right. Blue link row below:
  "Learn how to get custom ringtones" #3965FF.
- "Vibrate" heading + toggle ON.
- "Play a sound for" heading + checkboxes (Key press / Lock and unlock /
  Camera shutter, all checked): 20px box, blue border+check when checked.

### about.png
- "Device name" heading (15px) + value row "Windows phone" (gray label+
  ink value inline) + "Edit name" button (x12..104, h30, #CECFCE).
- "Device information" heading + rows: Model: Microsoft Virtual /
  Carrier: Operator / Software: Windows 10 Mobile / Installed RAM: 2 GB
  + "More info" button.

### usb.png
- "USB" page title heading + info paragraph + 2 toggles (both OFF):
  "Notify me if my mobile device is charging slowly over USB" /
  "Ask me before allowing other PCs to connect to my mobile device using USB".

### start.png
- "Preview" heading + live-tile preview mock (blue tiles in a phone frame);
  combobox flyout open: "My pictures"/"Sample images"/"None" (None selected
  = blue selection band).

### settings_update_active_hours.png (+ hourpicker)
- "Active hours" title + 2 paragraphs; "Start time"/"End time" labels +
  3-cell time pickers (5|00|AM, 11|00|PM); Save/Cancel buttons.
- Hour picker overlay: 3 columns (hour/minute/AM-PM), selected row = solid
  blue band across all columns with white text, up/down chevrons,
  check + X buttons on a gray bar.

### settings_search.png
- Search box with query "te" + X clear; suggestion strip (gray, words:
  test yes yeah tell yea the); result rows (icon+label): Apps for
  websites / Battery / Date & time / Phone update; keyboard docked.

### edit_device_name_popup.png
- FLOW PAGE (not a dialog): #F7F3F7 bg, "Edit device name" title y50 (20px),
  field y70..100 (2px accent border, "Windows phone", caret, X clear),
  Save/Cancel buttons y154..184 (x12..160 / x164..308, #CECFCE), SIP below.

## Palette (light)

| Role | Value |
| --- | --- |
| Page bg / header | #F7F3F7 |
| Deep-dive content bg | #FFFFFF |
| Accent (gears, toggles, links, sliders) | #3965FF |
| Ink (titles, labels) | #000000 |
| Secondary/description/group headings | #636163 |
| Button fill / toggle OFF fill | #CECFCE |
| Combobox border / separators | #9C9A9C |
| Scrollbar | #C6C3C6 |
| Search placeholder | #767676 |

## Port architecture (same shared-DLL model as edge/files/cortana)

- `settings/app/markup/*.xml` -> settings.uxaml (uxaml_gen.py)
- settings.dll = settings_app.c + settings_icons.c (stroke glyphs,
  files_icons.c style) + edge/app/uxaml.c + payload/phone/uixctl.c + svf.c
- FINGERKB.DLL (search + edit-name flows) + SHELLCHROME.DLL navbar
- Pages: HomePage, CategoryPage, DisplayPage, ColorsPage, SoundsPage,
  AboutPage, UsbPage, StartPage, ActiveHoursPage (+ picker overlay),
  SearchPage, EditNamePage.
