# Edge-on-NTOS worklog

One big log, newest entries at the bottom. Date format YYYY-MM-DD.

## 2026-11-10 — session 1: repo access, triage tooling, first Edge UI map

### Access + environment
- Cloned https://git.xania.fr/Phoebe/NTOS_SH4 (main), push verified (probe commit 0aaf263).
- Sandbox: Debian, no root; pip works; github.com + codeload reachable, github RELEASE
  asset downloads 404 (proxy), ziglang.org + pypi + deb.debian.org OK.
- Tooling installed via pip: pefile capstone lief meson ninja cmake.
- rizin v0.9.1 + rz-ghidra v0.9.0 building from source in background
  (scripts/build_rizin.sh, outside repo). Bootstrap script for resume: edge/bootstrap.sh.

### Reference artifacts (refs/edge/)
- Microsoft.MicrosoftEdge_38.14393.0.0_x86__8wekyb3d8bbwe.zip -> edgeapp/
  Windows.Mobile UWP package (Edge mobile RS1/1607, build 2016-07-16).
  Main modules: MicrosoftEdge.exe 5.5MB, eView.dll 5MB, eModel.dll 3.6MB,
  EdgeContent.dll 2.7MB, eData.dll 553KB, MicrosoftEdgeCP.exe 61KB,
  resources.pri 4MB, 90 language folders, Assets (incl. BrowserMDL.ttf,
  ReadMDL2.ttf, splash/tile PNGs, ErrorPages htm/css/js).
- DLL.zip -> dlls/DLL: Chakra.dll 5.8MB, Windows.UI.Xaml.dll 14.7MB,
  edgehtml.dll 19.9MB, edgeIso, eShims, edgeangle, iertutil, VCLibs app CRT.
- OLD_IE.zip -> oldie/OLD_IE: IECEExt.dll, coredll.dll, explorer.exe (IE4 WinCE era).

### Component graph (verified with pe_scan.py, see edge/analysis/pe_scan/)
- MicrosoftEdgeCP.exe: tiny launcher, delay-imports EdgeContent.dll **ord1**
  (matches provided tree). Also imports iertutil ords + PSM app-state APIs.
- EdgeContent.dll: delay-imports EDGEHTML.dll { InitializeLocalHtmlEngine,
  GetWebPlatformObject, MatchExactGetIDsOfNames, ord101, ord106 }, eData.dll
  { CreateUriToAppIdMappingManager, CreateTopSitesManager, CreateFavoritesManager },
  d2d1 ord2, dcomp DCompositionCreateDevice2, WININET/urlmon/CRYPT32...
  Exports ord1 + IE* shims (IEIsProtectedModeProcess etc). Has .ssm_url section.
- MicrosoftEdge.exe: UI process. Delay-imports EMODEL.dll + EShims.dll.
  Uses DWrite, RoActivateInstance/RoGetActivationFactory, WinRT error strings.
- eModel.dll: 47 exports (SpartanCore.* WinRT classes per AppxManifest).
  delay-imports eData.dll (favorites/topsites/uri-mapping), ESENT.dll (Jet db),
  XmlLite, WININET, WS2_32...
- eView.dll: imports EMODEL.dll directly; delay-imports eData.dll.
  Registers SpModel.ViewModel.* WinRT classes (BrowserViewModel,
  AddressEditBoxViewModel, TabsViewModel, NavigationViewModel, ...).
- eData.dll: ESENT-backed data store. Exports CleanUpDataStoreAsync +
  CreateFavoritesManager/CreateTopSitesManager/CreateUriToAppIdMappingManager.
- Chakra.dll (from DLL.zip): JS engine (ChakraCore ancestor) — absent from app
  package tree, supplied separately. Will be stubbed with mquickjs on device.

### resources.pri reversed (enough for UI mapping)
Format "mrm_pri2": header 0x20 bytes {magic, ver 0x10000, filesize, flags 0x20,
hdrlen 0x1560, checksum}; section directory = 32-byte entries
{char name[16]; u64 0; u32 offset; u32 size} at 0x20. 170 sections:
1 decn_info (header itself), 1 pridescex, 1 hschemaex, 1 res_map2_, 166 dataitem.
Sections nest: each carries a 0xDEF5FADE header + parent-size chain.
- hschemaex @0x1418: UTF-8 strings — full resource-key list (automation names).
  -> edge/analysis/pri_hschema_strings.txt (1939 keys)
- dataitem[0] @0x9b070 (380KB): the XAML metadata string heap — namespaces
  (using:SpartanXAML, using:SpModel.ViewModel), class names (SpartanXAML.*),
  control names (m_*), style/brush names, visual states, property paths.
  -> edge/analysis/pri_xaml_heap_strings.txt (3734 strings)
- res_map2_ @0x174b8: references "Files/<name>.xbf" — 41 XBF pages listed.
  NO XBF2 payload magic anywhere in package (AppxBlockMap: 377 files, 0 .xbf);
  XBF binaries not shipped in this tree — UI must be reconstructed from
  metadata + heap strings (this is our "blind" mapping job anyway).

### Edge mobile UI map (from pri analysis)
41 XAML pages/controls: SmallScreenPage (phone main UI), LargeScreenPage,
TabCenterPage, JournalPage, TravelBar (back/fwd/refresh), AddressEditBox,
OneBoxLite, Tabs/TabItem/TabThumbnailItem/TabProgressBar, HubPanel +
FavoritesPane/HistoryPane/ReadingListPane/DownloadsPane, Settings/MobileSettings,
PdfBar/PdfBarSmallScreen, FindOnPageBar, NotificationBar/NotificationDialog,
PromptDialog/InformationDialog/PasswordDialog, AddToFavoritesPage,
AddToReadingListPage, SaveFavoritesForm/SaveReadingListForm, ImportFavoritesPanel,
DeleteHistoryPanel, FeedbackPanel(Mobile), LeftBadgeBox/RightBadgeBox,
FavoriteButton, DCompVisualElement, VaultPasswordsPane, WebsitePermissionsPane,
Styles/SharedStyles/App.
Automation keys confirm full menu structure (ActionsMenu*, TabsContextMenu*,
Hub pivots, FindOnPage options incl. Arabic matchers, PDF annotation buttons,
Settings options like NewTabPageDisplayOption_TopSitesAndSuggestedContent /
_BlankPage, HomeButton toggle, ReadingView fonts/styles, Vault passwords...).

### Next
- decompile key entry points (rizin+rzghidra) once built: MicrosoftEdge.exe
  WinMain / App ctor, eView BrowserViewModel, EdgeContent ord1 export.
- design uXAML binary format + NTOS_SH4_linux loader.

## 2026-11-10 — session 1 (cont.): NTOS_SH4_linux + uXAML + Edge POC working

### Toolchain complete
- rizin 0.9.1 + rz-ghidra 0.9.0 built from source (meson options changed in
  0.9: -Denable_tests=false -Dsubprojects_check=false; rz-ghidra v0.9 is
  CMAKE not meson). Installed at /home/z/my-project/opt/rizin.
  Smoke-tested: `aaa; s 0x180001000; pdg` decompiles sample.dll RunApplication.
- Prebuilt tarball pushed to branch `edge-tools` (27.5MB):
  refs edge-tools:rizin/rizin-linux-x86_64.tar.gz — bootstrap.sh restores it.
- zig 0.14.0 at /home/z/my-project/opt/zig (340MB, redownloaded by bootstrap).
- Sandbox kills background daemons (nohup insufficient); long builds must run
  in foreground Bash calls; ninja resumes across calls.

### NTOS_SH4_linux (edge/ntos_linux/) — Wine-like host env, WORKING
- PE32+ loader: mmap at preferred base, DIR64 relocs, export dir parse,
  NTOS-model IAT patching via exported __imp_* slots (payload sources use
  payload/ntimport.h's exact NT_IMPORT/NT_DEFINE macros; host build adds
  dllexport on slots with -DNTOS_LINUX_BUILD). Forwarder-RVA check must
  inspect content (lld over-reports export dir size — data slots live inside
  it). Ordinal table holds EAT indices directly (no base subtraction).
- Builtin COREDLL (ms_abi gcc functions, Win64 ABI bridge): RegisterClassW,
  CreateWindowExW, Show/Update/Invalidate/Destroy, GetMessageW (script-idle
  hook + generated WM_PAINT), Begin/EndPaint, CreateSolidBrush/FillRect/
  DeleteObject (brush cache), SipShowIM/SipGetInfo, CreateFileW/ReadFile/
  WriteFile/CloseHandle/GetFileSize/SetFilePointer/FindFirst/Next/Close/
  DeleteFileW (\fls0\ rooted at --fs), Sleep, GetTickCount, mem*.
- e2e scripting: tap/key/char/sip/shot/quit lines execute lazily when the
  app queue drains (interleaved with paints). PNG writer = stored-deflate,
  no zlib dependency. 320x528 LCD, 0x00BBGGRR pixels.
- sample_app.c validates the chain; pixel-verified chrome (status bar,
  address pill, nav buttons, tap/key state changes).

### uXAML v1 (edge/app/uxaml.[ch] + uxaml_gen.py)
- Format UXA1: header + doc table + len-prefixed string heap + forward-only
  node stream (kind u8, attrs {id,type,payload}, child_count). Static arena,
  zero heap, single pass, ~200 lines of C.
- uxaml_gen.py build: XML subset -> binary (4.8KB for 5 pages); toxml:
  lossless "upper" back to XML. Round-trip verified in CI.
- Layout v1: coordinates relative to parent; StackV/H + List/Row auto-flow;
  Canvas/Button children relative. (Bit me once: markup with absolute coords
  inside buttons rendered offscreen — all markup now relative.)

### Edge POC (edge/app/edge_app.c + markup/) — WORKING, 11-frame e2e
- Pages: SmallScreenPage (status strip, NTP top-sites grid, bottom
  [tabs][address pill][...] chrome), ActionsMenu sheet (12 real items),
  TabCenterPage (dark, cards + close), HubPanel (4 pivots), AddressEditBox
  (+ QWERTY SIP, tap-typing, Go navigates).
- Mock engine: URL -> hash -> generated article; NTP when empty; tabs model
  (open/close/switch); commands routed by markup Command attr.
- e2e script tests/edge.script: boot, menu open/close, tabcenter open/close,
  address mode, keyboard typing, navigate, hub pivots — all frames
  pixel-checked (check_shots.py), non-blank, correct palette counts.

### CI (.forgejo/workflows/edge-ci.yml)
- Builds ntos_linux + sample + edge app with zig, runs both e2e suites,
  uXAML round-trip; ships log + screenshots to `edge-ci` branch.

### Next
- real decompilation passes (MicrosoftEdge.exe App::OnLaunched, eView
  BrowserViewModel) to refine chrome details before screenshot comparison.
- mquickjs evaluation for the chakra.dll stub; EdgeContent ord1 stub shape.
- SH4 device build of the edge app (payload.ld + elf2ntx) once the loader
  side is wired into phone.mk flow.

## 2026-11-10 — session 2: UI direction correction (dark theme, reference chrome)

User feedback on session-1 POC: chakra is secondary (device has no FPU —
stub only), the UI is the product and must match the real Edge mobile
reference: dark theme, bottom address bar [tabs] | [reload] [lock+url]
[reader] | [...], menu with 4 hub icons at the bottom + options list,
tab center with 2-col grid + right-padded toolbar, Add-to-reading-list and
Clear-browsing-data screens. Screenshots of session 1 were "all over the
place" — root cause found and fixed this session (see below).

### CI fixed: chakra.dll is a pure API-shape stub
- CI broke on sandbox-local mquickjs path. chakra.dll is now GENERATED from
  the real export table (tools/gen_chakra_stub.py -> 160 exports + smoke
  test, all E_NOTIMPL except singleton handle plumbing). mquickjs variant
  moved to chakra_mqjs.c, optional local target chakra-full.dll.

### Ground-truth strings + activation map (edge/analysis/)
- resources.pri has the en-US display-string blob @0x32c208: exact wording
  for every menu item, settings row, clear-data checkbox, tabs context menu
  (pri_display_strings.txt, EDGE_UI_STRINGS.md).
- WinRT activation map per module (winrt_activation_map.txt): MicrosoftEdge.exe
  activates 445 SpartanXAML/SpModel classes (pages + view models + converters);
  eView registers SpModel.ViewModel.* factories; eModel serves SpartanCore.*.
- AddressBarHighlightedTextBrush vs NonHighlightedTextBrush confirm the
  two-tone URL rendering.

### UI rebuild (edge/app/markup/ — 11 pages, all dark)
SmallScreenPage (bottom bar per reference), ActionsMenu (7 options + 4 hub
icons), TabCenterPage + TabsContextMenu (7 real options), HubPanel (4 pivots),
AddressEditBoxPage (+dark SIP, suggestions), SettingsPage (sync toggle,
rows with values/chevrons), ClearBrowsingDataPage (6 checkboxes, Clear,
cloud/Bing links), AddToReadingList/AddToFavorites dialogs, FindOnPageBar.
icons.[ch]: 20 MDL2-style outline glyphs, integer-only strokes.
Mock engine: wikipedia article (title + TOC + link-blue segments), generic
site page, dark NTP (4x2 top sites + news cards).

### Two hard bugs found (the "all over the place" report)
1. layout_doc accumulated Stack/List flow offsets in the REVERSE child
   loop -> every list laid out bottom-up (menu rows inverted, hub/settings
   scrambled). Fixed: two-pass (assign rects in doc order, then descend).
2. v1 markup used absolute coords inside Buttons; children are
   parent-relative (XAML Grid semantics) -> bar/toolbars painted offscreen
   (y=972). All markup now uses relative child coords; right-aligned Text
   uses x=0 + w=<right edge> + align=2.

### e2e
16 scripted frames with per-frame exact pixel expectations
(tests/check_edge_shots.py): boot/address/wiki/menu/readlist/settings/clear
(+toggled/toast)/hub x2/tabcenter x3/tabsmenu/final — all pass locally.
CI runs the same via make -C edge/app clean all test.

### New refs uploaded by user (refs/edge/, not yet analyzed)
Microsoft.Windows.SystemSettings (3.7MB), FileExplorer (1MB), Cortana
(27.9MB) — next targets for the uXAML "windows settings" story.

### Next
- SystemSettings package triage (pages, pri strings) -> uXAML settings app.
- Compare against the user's reference screenshots when provided.
- SH4 device build of the edge app; maybe SH4-targeted size audit.

## Session 4 (2026-11-10) — reference screenshots + uXAML v2 + shared DLLs

### Reference captures (refs/edge-screens, 15 shots @320x528)
Pixel-measured every screen (edge/analysis/EDGE_UI_REFERENCE.md; probe
tools in edge/tools/): palette (#1F1F1F bars/keyboard, #2B2B2B sheets,
black hub/settings pages, #333 keys/buttons, #767676 separators,
#0078D7 accent, #7F7F7F scrim), exact rows/icons/checkbox geometry.

### UI rebuilt to the measurements
Boot = white page (home.png), menu sheet 157..419 + icon bar 420..476,
tab cards 142px at x12/x166 pitch 148 (24px #1F1F1F header, black
thumbnail), hub pivots (icons 22/66/110/154 centers, 3px blue underline
y42, HISTORY + "Clear all history"), clear-data page (6x44 checkbox
rows, 20px accent boxes, Clear x12-177 y394-425, blue links), dialogs
(READING LIST, #333 hero y78-257, bordered field, Add/Cancel
x12-157/x162-307), InPrivate page (#1F1F1F, heading/body/#009BE4 link),
find bar (input + chevrons + ...), two-tone URL with lock.

### Component reuse landed (the user's shared-DLL model)
- Render vtable gains a UIX_TH_* theme table: uixrender publishes the
  light table, edge publishes dark. fingerkb + uixctl read it, so ONE
  keyboard/control implementation serves Settings (light) and Edge.
- FINGERKB.DLL now builds as a host PE module: ntos_linux loads it
  beside edge.dll, the loader snaps its render/svf imports to edge's
  exports and edge's __imp_UIXGetSchemaFactory to the keyboard factory.
  The address editor shows the real shared SIP (99.2% vs type.png).
- payload/phone/uixctl.c: shared button/checkbox/toggle/field painters
  (reference geometry); settingsui.dll imports them via ctl_ prefix
  (UXCTL.DLL in the device manifest); edge links them statically.
- ntimport.h: IAT slots dllexport under NTOS_LINUX_BUILD (host loader
  can patch them); NT_IAT_EXTERN_ONLY for multi-object host DLLs.

### Host loader bugs found (both latent)
- OptHeader64.Win32VerValue was declared uint16_t -> SizeOfImage and
  everything after it shifted 2 bytes; the bogus ~1.7GB reservations
  only collided once multiple DLLs loaded at 0x18xxxxxxx bases.
- main.c passed `(PeModule**)mods` — punning the struct array as a
  pointer array; others[0] read the module NAME as a pointer. Only
  crashed the first time an import missed the builtin table
  (UIXGetSchemaFactory), which never happened before cross-DLL reuse.

### e2e v2 (tests/edge.script, 16 frames)
12 frames map 1:1 to reference captures; check_edge_shots.py scores
block similarity (16x16, tol 48, >=80%) + colour anchors + NEW
glyph-presence guards (caught a full "text missing but colours match"
regression that block-averaging alone had hidden — markup children were
absolute, not parent-relative). Scores: boot 99.8, menu 99.5, address
99.2, wiki 95.5, readlist 99.8, tabs 99.8, manytabs 99.7, clear 100,
clear-toggled state, hub-history 99.2, hub-downloads 100, inpriv 99.8,
findbar 100. 'key s' is the POC shortcut into Settings.

### Next
- Push the phone SH4 build through build_phone.py (UXCTL.DLL manifest
  is wired; needs the device toolchain run).
- More hub panes polish (favorites rows vs real capture), settings
  page reference capture if the user provides one.
- SystemSettings/FileExplorer ports on top of uixctl + uXAML.

## Session 5 — reference parity v4/v5, settings on uXAML, nav bar DLL, md engine

Quality pass over the full reference set + the three feature blocks the user
called out (settings/toggles/dropdowns/nav bar, keyboard context, md pages):

- **Shared components** (`payload/phone`): ctl_toggle rewritten as the W10M
  pill (ON=accent+white thumb right, OFF=outline+grey thumb left, dim=
  disabled look); UIX_TH_NAV_BG/NAV_INK theme slots; Keyboard.context (go-key
  input scope: arrow default, search magnifier for find fields — type.png);
  SYS_GO_ARROW VEC1 asset; SHELLCHROME gains UIXChromeFactory (hosted-app
  chrome: nav bar only, themed through the host renderer).
- **Edge chrome v4** (user feedback, stroke audit): stroke weight size/16+1
  (2px toolbar, 1px card glyphs); "..."=3px dots; + arms to 45% box;
  InPrivate striped body; tab cards 30px header with the 1px #1F1F1F
  minimized-tab frame (L/R/bottom, tabs.png x12/x153 bottom y189); #8A8A8A
  card titles; TABS caps title; address bar at reference positions (hint
  x98, lock/priv-glyph x104, text x124); private tabs show the small blue
  InPrivate glyph; menu sheet reaches y480 (white-gap fix); tab-center
  toolbar at x160/x228/x289; wiki/mock links underlined.
- **Settings family rebuilt on uXAML** (12 new references): 3 pages + 2
  flyout docs; UXA_UNDERLINE/UXA_DISABLED; dropdowns with live selection
  (dyn_color/dyn_text); drag scrolling + fading scrollbar; the nav bar from
  the imported SHELLCHROME chrome on every settings screen (back walks the
  stack); checker carries state-discriminating presence probes because the
  block metric alone cannot tell the pages apart. Ratios 84-99.4%.
- **Host fixes**: INVALID_HANDLE_VALUE sign-extended (load_nav/read_file
  segfault); harness `drag x0 y0 x1 y1` script command; SIGSEGV RIP logger.
- **Text-page engine** (mdrender.c, successor of md_viewer.py): one run
  emitter for md and HTML4 subsets; word wrap into shared visual lines;
  find-in-page with yellow/orange matches over the visual-line table;
  <script> bodies forwarded to the CHAKRA.DLL stub (JsRunScript ->
  E_NOTIMPL, nothing executed); svf images inline (logo.svf demo asset).

States in CI: 27 shots, 21 against device references, all >= 80% blocks
(16x16/48 tol) + presence probes. Next: FileExplorer port reuse of UXCTL.

## Session 6 — the 8px nav gap, md layout quality, find repaint

User-driven quality pass (every light-mode screen had an 8px glitch band;
md pages regressed on margins/lists/images; find highlight hid the text):

- **Nav bar owns the full strip** (shellchrome.c): navigation_paint covered
  only y488..528 (40px) while the system bar is y480..528 — the 8px slice
  y480..487 was never repainted, so the last keyboard frame bled through
  under every light-mode toolbar (light-inpriv's black bar, md-reader's
  clipped SIP). Now fills y480..528 and the icons are re-centred on the
  reference captures: back arrow (2px shaft y503-504 + 3px chevron), the
  W10 window-flag home glyph (right pane taller, 1px centre split), 2px
  magnifier ring + handle — measured from light/inprivate.png and
  settings_1.png. Dark NAV_INK is the reference grey #BDBEBD, not white.
  Hit zones y>=480, navheight 48.
- **MARKDOWN engine layout fixes** (payload/phone/markdown.c):
  - UA body margin 4px (browser default): applied at the root for md /
    body-less html, via TG_BODY UA margins when a real <body> exists —
  text never touches the screen edge; author CSS still overrides.
  - tall inline images grow the line box BEFORE anchoring: a 64px svf
  used to anchor its top 47px above the line start, overlaying the
  blocks above (edge-mdpage svf-over-text bug).
  - ordered-list markers right-align against the content box (text_w
  measured); "1." used to sit at x=-2 and clip.
  - raw <script>/<style> inside markdown: forwarded to the CHAKRA hook /
  CSS parser and never rendered (the alert() body used to paint as
  text); raw <hr> lines open a real HR block; [ref]: definitions no
  longer leak ": url" into paragraphs.
  - bare http(s) URLs linkify: the plain-run scanner now stops before a
  URL (it used to swallow it into black text), and the bare-url/
  autolink branches carry a depth guard — a URL as link text re-entered
  the branch with identical input and blew the stack (ASan-confirmed).
  - md_find_text(idx): new export returning the matched source text
  (original case) so hosts can repaint it over the opaque highlight.
- **find-in-page repaint** (edge_app.c): the yellow/orange boxes are
  opaque, so the matched text is redrawn on top in black — matches stay
  readable (no alpha on this display).
- svf page art: black-tinted through the pigment-aware ink() (white VEC
  art becomes visible black; coloured art keeps its palette).
- settings scrollbar stops at y477 (above the nav bar); nav-glyph probes
  re-centred on the new icon geometry with the grey-ink threshold.
- MDSmokeTest: the find check could never pass (zeroed host has no
  text_w — silent since day one); it now runs with a metric hook and
  also verifies md_find_text. 0x100F = all four.

States in CI: 44 shots, all reference-matched states >= 94.5% blocks,
probes green. Local: chakra + markdown smoke entries pass.

## Session 7 — NTOS_SH4_windows: the user's-PC iteration loop

Goal (user): "CI to build a windows version (or a web version) so I can test
it on my computer, see what's rendered, debug it, provide better icons."
Delivered as edge/ntos_win — the Win32 twin of the Linux host:

- **Same payload, same bytes.** pe_loader.c gained a portable map_image()
  (VirtualAlloc vs mmap); win_core.c is a builtin-for-builtin twin of
  host_core.c (WinCE FIND_DATA layout, \fls0 mapping, SIP rects, launch
  hook, repatch_shared) so payloads cannot tell the hosts apart.
- **Interactive mode.** No --script: a desktop window mirrors the 320x528
  LCD (auto 1x/2x from the workarea), real mouse/keys become
  WM_DOWN_/MOVE_/UP_/KEY_/CHAR_, F12 saves shot-interactive-NNN.png,
  Ctrl+Q quits. get_message_w pumps Win32 inside the CE wait, so the
  payload sees input exactly like a stylus.
- **Session recorder.** Every tap/drag/key/char is appended to
  session.script in the ntos_linux e2e format — a manual bug hunt becomes
  a replayable CI script. LCD parity is bit-exact: wpar.py fails the build
  if any frame of the same winboot.script differs between hosts.
- **CI**: edge-ci builds ntos-windows.zip (exe + shared DLLs + fake \fls0 +
  run-edge.bat + README), runs it under wine64 (Debian's wine user32 needs
  zlib1.dll from libz-mingw-w64 — nothing ships it otherwise), and pushes
  the zip + wshot pngs to the edge-ci branch. Local sandbox reproduced the
  exact CI flow (relocated wine, patched nls/wineserver paths): all 7
  winboot frames byte-identical, interactive session stays alive and
  records.

## Session 7 (cont.) — find repaint fidelity, two-tone URL, tab-card icons

The user's four chrome-nits batch, each verified by eye (vision model)
against the fresh e2e shots after the fix:

- **find-in-page highlight repaint** (markdown.dll + edge): new export
  md_find_style(idx,&fh,&flags) returns the metrics of the op that starts
  each match — the box now grows to the full line box (fh+4) and the text
  is repainted in its own run: a bold 22px heading repaints as bold 22px,
  greyed code lines as laid out. MDSmokeTest grew two checks for it
  (0x103F = all six). VLM verdict on edge-mdfind.png: "renders at same
  size/weight as page text, not clipped or offset".
- **find bar typed text** was hardcoded black — theme ink now (white on
  the dark bar, like the markup's FindText node).
- **two-tone address URL**: registrable domain = last two labels —
  "en." grey, "wikipedia.org" bright ink, path+query ("/wiki/Page") grey,
  bare 2-label hosts stay all-bright. The old code split at the FIRST dot
  and never painted the path.
- **tab card headers** (light-tabs): favicon was 21px off the card edge
  (VLM: icon x33 vs reference x22) — now 11px like the reference, title
  6px behind the icon, X 4px from the card's right edge.

Bottom-bar audit (navbar_audit.py/stripdump.py over all 44 CI shots +
references): the per-screen bar variation is reference-true — dark
browser = edge app bar full-bleed, light browser = toolbar raised 48px
over the shared SHELLCHROME nav bar (the only bar painter, in edge AND
settings), tab center = its own toolbar, hub/clear/readlist = empty black,
SIP-up = keyboard through the strip. light-tabs now keeps its reference
(tabs_menu.png) shape: raised toolbar + chrome bar below.

## Session 7 (cont.) — HTML form controls + ACID2 staging

- **Form controls** (markdown.dll engine, no host changes):
  - <select> (incl. multiple) swallows its options via a scan-ahead in
    html_parse and renders one field with the selected label + a chevron;
    last `selected` wins, first option as fallback. Labels are trimmed
    and newline-flattened (real-world markup indents them).
  - <textarea>: scan-ahead value, cols/rows -> box size, value painted
    line by line inside (only rows that fit).
  - type=password renders dots, never the secret; type=hidden AND the
    HTML5 `hidden` boolean attr render nothing (attr order independent);
    type=reset/image fall back to submit-style buttons ("Submit" label
    when valueless); size=N sets control width (input and select).
  - checkbox tick redrawn as a bold 2px diagonal checkmark (the old 1px
    "L" was invisible at page zoom); select labels clamp before the
    chevron (text_w-aware).
  - Verified by eye (vision model): third checkbox ticks, radio 2 carries
    the dot, "Selection Item 4" sits inside the box with a chevron, no
    garbled text. e2e states: form, form-scrolled (form.test mock page).
- **ACID2 staging**: acid2.test serves the real test page (page source in
  tests/assets/acid2.htm, generator reads repo assets now — the mdtest/
  sandbox paths died with that sandbox; array sources restored from the
  committed mockweb.h). Current render status, honestly: blank above the
  fold. md_load parses it (43 blocks / 65 rules) and layout runs, but the
  em-heavy geometry (margin: 100em, width: 140%) wraps the int16 op
  coordinates — all 63 ops land at wrapped-negative x/y and paint
  nothing at any scroll. parse_len now saturates at +-3000px; the rest
  needs the ACID2 milestone: int32/saturating layout arithmetic,
  position:absolute/fixed, display:table, min/max-*, line-height,
  margin-collapsing, data:URI PNG inflate (the eyes), object fallback.
- **Forms + engine hardening share the smoke**: MDSmokeTest 0x103F.

## 2026-11-11 — session 3: Windows CI artifact + no hardcoded pages + swap gate

### Windows build artifact (user iteration loop)
- CI `make package` died at `zip: not found` (exit 127) — the runner image
  has no zip(1). New edge/ntos_win/mkzip.py: stdlib zipfile packager, same
  flat layout, deflated, fixed timestamps (reproducible), CRLF-safe bat.
  The edge-ci workflow already shipped ntos-windows.zip to the edge-ci
  branch; with packaging green the bundle is downloadable on every push.
- markdown.dll renamed to **html.dll** everywhere (module name only; the
  md_* exports and the payload/phone/markdown.c source are unchanged —
  the loader cross-snaps by symbol). The DLL is the POC's real
  HTML4/CSS1/Markdown renderer and now says so beside chakra.dll: a
  plain engine library (fill an MdHost, call md_load/md_layout/md_paint)
  that any app or test harness can host. Dead code removed: edge/app/
  mdrender.c (the pre-box-model line renderer) built by nothing.

### No hardcoded pages — everything renders through the engine
- Deleted paint_wiki() (the static "Exam" token list that mimicked
  webpage.png) and the hardcoded grey-bar unknown-host page. There is no
  procedural page painting left in edge_app.c.
- mock DNS: wikipedia.org serves the real Casio ClassPad 300 article
  (wiki_cp300.md); the tab title comes from md_title().
- Unknown hosts: a real HTML error page naming the requested host is
  generated and engine-rendered ("can't reach this page", #A2A9B1 border
  asserted pixel-exact in e2e).
- Fixed: reader view leaked across navigations (wiki-cp300/acid2/form
  rendered warm-paper after the md-reader toggle). Reader is per-page:
  web_load() and the file:// path reset it, as real browsers do.

### Render-from-content gate (the "swap the values" acceptance test)
- tests/assets/swap.html: bg #0000FF, h1 #FF0000 @22px, text #00CC00.
  The e2e Makefile derives swap2.html by sed-swapping the VALUES (bg
  #FFFFFF, h1 #0000FF @16px, text #CC0000); both render through the
  engine; check_edge_shots.py asserts the pixels followed:
  - new colors_ok probe type: counts pixels within tol of the authored
    RGB (negative need = forbidden zone)
  - the 16px heading must have far fewer glyph pixels than the 22px one
    (320 vs 487 measured) and must not reach the body zone
- The gate caught two real engine bugs on its first run, both fixed in
  markdown.c:
  1. canvas didn't cover the viewport — short pages left stale pixels
     below the content. md_paint now fills the canvas across the whole
     viewport before ops paint.
  2. CSS canvas propagation — a bg-less <html> stopped the canvas scan,
     so body{background-color:X} painted only its own box. html's bg
     propagates; without it, body's does.
  (It also caught a sed bug in the test itself: 'font-size:22px' didn't
  match 'font-size: 22px' — the render was right, the test value wasn't.
  Good sign: the gate discriminates.)
- swap.html + swap2.html ship in the ntos-windows bundle so the same
  proof runs on the Windows host by hand.

### State
- MDSmokeTest via html.dll: 0x103F (unchanged). edge e2e: all shots
  green incl. new wiki content probes, errorpage, swap-a/b. files-app
  e2e green. ntos_linux sample e2e green. uxaml round-trip green.
