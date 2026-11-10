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
