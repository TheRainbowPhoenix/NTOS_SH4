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
