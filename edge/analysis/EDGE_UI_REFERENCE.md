# Edge mobile UI reference — pixel measurements (refs/edge-screens)

Device: W10M-era Edge (38.14393.0.0), dark theme, 320x528 capture.
All measurements from pixel analysis of the real-device screenshots
(`refs/edge-screens/*.png`). Probe scripts: `edge-tools` branch
(`scripts/analyze_screens.py`, `scripts/probe.py`, `scripts/cmp_shots.py`).

## Palette

| Role | Color |
|---|---|
| Address bar / toolbar / keyboard backdrop / InPrivate page | `#1F1F1F` |
| Menu sheet / flyout list | `#2B2B2B` |
| Tabs page / hub pages / settings pages background | `#000000` |
| Tab-card header strip | `#1F1F1F` |
| Keyboard key | `#333333` |
| Keyboard special key (shift, &123) | `#4C4C4C` |
| Enter/search key | `#FFFFFF` (black glyph) |
| Separator (address bar) | `#767676`, 1px, y491-518 |
| Accent (checked box, links, pivot) | `#0078D7` |
| Checkbox border (unchecked) | `#CCCCCC`, 2px |
| Scrim over white content | `#7F7F7F` (50% black over white) |
| Web page content | `#FFFFFF` |

## home.png (= boot state; NTP content not captured — render white)

- Content: plain white 0..479. Status bar area white/blank (no icons).
- Address bar: y480-527, `#1F1F1F`, 48px tall.
- Icons (x-ranges, y495-514): tabs `15-32`, reload `63-82`, reader `235-257`
  (disabled `#4C4C4C`), more dots `289-302` (y504-506).
- Separators: x=48 and x=271, y491-518.
- Placeholder "Search or browse": x98 start, y499-510, gray.

## context.png ("..." menu)

- Scrim y0-156 `#7F7F7F`; menu list starts y157.
- Menu list: y157-419, bg `#2B2B2B`, 6 rows x 44px. Text x13, white,
  ~15px cap height, text band y+23..+37 within row. "Go forward" dimmed.
  Rows (y-centers): 185, 230, 273, 316, 361, 405.
  Items: Go forward / Share / Add to favorites / Add to reading list /
  Find on page / Pin this page to Start.
- Icon bar: y420-476, bg `#1F1F1F`. 4 icons (centers x75, 144, 212, 280),
  y425-445; labels y458-472 (Favorites / Reading list / History / Downloads).
- Address bar: y477-527, same as home.

## tabs.png / manytabs.png (Tab center)

- Background pure black. Title "Tabs": x12, y18-29.
- Cards: 2 columns x12-153 and x166-307 (142 wide, 13px gutters).
  Card: header strip y+0..+27 `#1F1F1F` (page icon + title + close X),
  thumbnail area below = black. First card row starts y48. Card ~142 tall.
- Toolbar y480-527 `#1F1F1F`, 3 icons right-aligned group: new-tab "+" center
  ~163, InPrivate tab center ~237, "..." at 289-302 (same x as browser).

## type.png (address edit + SIP)

- Page dimmed `#7F7F7F` y0-222.
- Dark strip y223-229; white address field y230-269, x5-314 (`#FFFFFF`),
  placeholder gray, blue caret. No icons inside.
- Toolbar row y270-316 `#1F1F1F`, single tabs icon left (x~10-30), white.
- Keyboard y317-527 = FINGERKB.DLL geometry: rows 317+53k, keys 28x50,
  x pitch 32 (row1/2 start x2; row2 9 keys; shift x2-47, zxcvbnm x50..,
  backspace x276-318; bottom row &123 x2-47, :) x51-79, "," x83-111,
  space x115-240, "." x244-272, enter x276-318 white w/ black glyph).
  Backdrop `#1F1F1F` (gaps), keys `#333333`, special `#4C4C4C`.

## clean_hist.png (Clear browsing data — fullscreen page, black bg)

- Title: x13, y16-29.
- Checkbox rows: 6 rows, pitch 44, first at y66. Box x12-31 (20px).
  Checked = `#0078D7` fill + white check; unchecked = 2px `#CCCCCC` border.
  Labels white, x41. First 3 checked (Browsing history / Cookies and saved
  website data / Cached data and files), then Download history / Form data /
  Passwords.
- "Show more" y338-345. Clear button: x12-177, y394-425, bg `#333333`,
  white label. Links (blue, underlined): y457-479 two-line
  "Change what Microsoft Edge knows about me in the cloud"; y490-519
  "Clear Bing search history".

## history.png (Hub — History pivot; hub pages bg black)

- Pivot icons y10-32, centers x22, 66, 110, 154 (~15px icons; white,
  active one blue). Blue underline y42-44 x88-131 (under active icon).
- "HISTORY" x12 y61-71; "Clear all history" blue right x219-306 y62-74.
- Group header "Last hour" y106-116. Rows ~42px: favicon square x15-35,
  title x44 y144+, time right-aligned, url gray below.

## edge4.png (Hub — Downloads, empty)

- Same pivot row; DOWNLOADS active (blue + underline).
- "DOWNLOADS" x10 y62-69; body "You'll see current and past downloads
  here." x10 y~115.

## add_reading.png (Add to reading list — fullscreen, black bg)

- "READING LIST" x12 y15-32.
- Preview area y78-257 bg `#333333`; white reading-list glyph
  x132-187 y173-222.
- "Name" label y280-295; input field x12-307, y298-330, 1px white border,
  black fill, white text.
- Buttons y358-389 (32px): Add x12-157, Cancel x162-307, bg `#333333`
  (`#2E2E2E` measured avg), white labels.

## inpriv.png (InPrivate new tab, bg `#1F1F1F`)

- "Browsing InPrivate" heading x24 y130-146 (white, large).
- Paragraph x24, 6 lines y183-294 (20-21px pitch).
- Blue link "Read the Microsoft privacy statement" y348-356.
- Address bar: same as home but with small InPrivate "e" square in field.

## webpage.png (loaded page)

- White content; article text per mock engine.
- Address bar: lock icon x106-119; URL from x129, two-tone: "en." gray,
  "wikipedia.org" white. Reader icon x235-257 (disabled tint).

## find next.png (Find on page bar)

- Bottom bar `#1F1F1F` like address bar: empty input area left half,
  chevron-left x158-168 y494-513, chevron-right x227-237, "..." x289-302.

## Block-similarity metric (user spec)

16x16 blocks, per-block average colour; block "differs" if any channel
delta > tolerance. Target: >= ~85% blocks matching per screen vs the
reference captures (antialiasing eases out at block granularity).
