# Files (FileExplorer) port worklog

One big log, newest entries at the bottom. Date format YYYY-MM-DD.
Same workflow as edge/WORKLOG.md.

## 2026-11-10 — session 1: full XBF-level analysis + light-theme POC working

### Reference artifact: Microsoft.Windows.FileExplorer_cw5n1h2txyewy.zip
W10M inbox File Explorer (10.0.14393.0, RS1, built 2016-07-16 — same branch
day as the Edge package). Small tree: FileExplorer.exe 2.1MB (x86 WinRT/CX,
wincorlib + RoGetActivationFactory-style imports, DllGetActivationFactory
export), resources.pri 524KB, folder PNG assets, BitMDL2.ttf.

### The XBF goldmine (unlike Edge, this pri SHIPS the XAML)
- `files/tools/pri_decode.py`: full mrm_pri2 decoder written from the
  chausner/PriTools layout RE (header/TOC, DEF5FADE nesting, hschema
  scopes/items tree with ascii+unicode name blocks, res_map2 candidates,
  dataitem span tables, 65-language qualifier resolution). Two bugs found
  the hard way: root scope self-reference must keep parent=NULL (infinite
  loop otherwise), and the display filter eats `[m` sequences so the TOC
  names look different from what they are.
- Output: `files/analysis/pri_en_us.txt` (234 en-US display values — every
  command label, dialog text, error string, exact font size/weight per
  named style) + `pri_resources.txt` (all 243 resources x languages).
- `files/tools/pri_extract_files.py`: carves the 17 XBF v2 pages + assets.
- `files/tools/xbf_dump.py` + `xbf_framework_types.py`: XBF v2 decoder
  ported from chausner/XbfTools (string/assembly/type/property/xmlns
  tables, node stream with all opcodes, style/resource-dictionary/
  data-template/deferred/visual-state sections, framework type/property/
  enum name tables auto-converted). All 18 pages decode, including the
  27KB DesignCore.xbf (2161-line shared style library).
- Port spec: `files/analysis/FILEEXPLORER_UI_REFERENCE.md` — full type
  ramp (13/15/17.333/18.666/20px, SemiLight/SemiBold), palette (light
  theme: ChromeMedium #E6E6E6 nav bar, ChromeLow #F2F2F2 pane), geometry
  (48px bars/buttons, 256px nav pane, 96px properties thumbnail) and the
  page architecture (SplitView + NavBar + ExplorerBrowser + FrameAppBar).

### NTOS_SH4_linux host extensions (edge CI re-verified green)
- FindFirstFileW/FindNextFileW now fill the REAL WinCE FIND_DATA layout
  (attrs + 3 FILETIMEs + size_hi/lo + oid + W16 name, 560 bytes — matches
  payload/coredll.c and phone.h), with stat()-backed sizes and mtimes,
  and skip the unix dot entries. INVALID_HANDLE_VALUE now returned
  zero-extended like CreateFileW (the old -1 crashed FindClose with a
  sign-extended handle — found via host-side tracing).
- New builtins: CreateDirectoryW, RemoveDirectoryW, MoveFileW, CopyFileW,
  GetDiskFreeSpaceExW (statvfs). All additive; the full Edge e2e still
  scores 95.5-100% block match against the real-device captures.

### The POC: files/app (light theme, like the W10M Files app)
- markup/*.xml -> files.uxaml via the shared edge/app/uxaml_gen.py;
  files.dll compiles edge/app/uxaml.c (shared uXAML engine) +
  payload/phone/uixctl.c + svf.c (shared themed painters) — same
  shared-DLL model as edge.dll, but publishing the LIGHT theme table so
  FINGERKB.DLL paints the light SIP.
- Pages: FolderPage (NavBar hamburger/breadcrumbs/Up + content +
  FrameAppBar with browse/select/dest rows), NavPane overlay (FILE
  EXPLORER + This device + quick folders + SD card), CommandsDialog
  (Copy to/Move to/Delete/Rename/Properties/Share/Sort by/View/Refresh),
  RenameDialog + NewFolderDialog bottom sheets docked above the shared
  keyboard, ConfirmDialog, PropertiesPage (thumbnail + 13px rows +
  drive Used/Free + 6px accent bar), SearchPage (live recursive filter).
- REAL file model on \fls0\: scan+sort (name/date/type/size, folders
  first), list & icons views, select mode with checkboxes, create
  folder, rename, delete (confirm dialog), copy/move with a destination
  picker — all through the new host builtins.
- files_icons.c: 28 MDL2-style integer-stroke glyphs incl. the amber
  two-tone Folder_Large folder.

### Three hard POC bugs (all found by pixel-diffing the e2e frames)
1. dyn_set stored pointers to STACK buffers — status texts turned to
   garbage by paint time. Fixed with by-value static storage.
2. flag_named only cleared NS_HIDDEN, so markup visible="false" buttons
   (the select/dest app bars) could never be shown: hit_test found the
   wrong buttons and the whole mode flow derailed. New show_named() sets
   NS_SHOWN / NS_HIDDEN as a full override pair.
3. Browse app-bar buttons stayed visible in select mode, overlapping the
   Select-all/Copy/Move/Delete row. The app bar now swaps wholesale.

### e2e + CI
- tests/files.script: 22 scripted frames (boot, navpane, browse, select,
  commands, properties, newfolder typed on the shared SIP, recursive
  search, delete + copy file ops) — check_files_shots.py verifies size,
  non-blank, palette anchors per screen (canonical host PNG reader).
- .forgejo/workflows/files-ci.yml: builds ntos_linux + files app with
  zig, runs the e2e + the pri-decoder self-test, ships the log and
  screenshots to the `files-ci` branch (same pattern as edge-ci; no
  shared state with edge data).

### Next
- Real-device screenshots to refine pixel values (same workflow as the
  edge session-4 reference pass), then block-match scoring against them.
- Picker mode UI (Done/Cancel app bar — the app doubles as the system
  file picker), sort-direction toggle, breadcrumb ellipsis flyout.
- SH4 device build of files.dll through build_phone.py / elf2ntx.

## 2026-11-10 — session 2: reference-parity pass (refs/files-screens)

### Toolbar: labels hidden by default (the user-visible fix)
The W10M CommandBar shows icon-only buttons collapsed; labels appear only
while the "..." overflow is open. LIGHT_THEME_this_device.png drove the
rework: 5 x 64px buttons (browse: Select / New folder / View / Search /
"..."; select: Delete / Move to / Copy to / Share / "..."; picker:
Done / Cancel / "..." right-aligned), glyph centred at y15 in the 48px
bar. The "..." state swaps to a second 68px markup row (glyph y8 + 11px
label y40) and grows the content clamp accordingly. The e2e now asserts
the label strip (y518..523) stays pure chrome in every collapsed screen
and that labels exist in the expanded screens.

### Sort + overflow menus (replaces the CommandsDialog)
- "Sort by: Name" bar with accent key + chevron (up while open) opens a
  6-row flyout (this_device__sort_by.png): Name/Size/Date x asc/desc,
  selected row in accent. sort_key + sort_desc replace the old 4-way
  cycle; folders still sort first.
- "..." opens the MenuFlyout: browse = Refresh / Select all / Clear
  selection (disabled) / Properties; select mode = Select all / Clear
  selection / Rename (enabled iff exactly 1) / Properties. Panels paint
  procedurally OVER the rows (the markup-rect version landed under the
  items — z-order lesson).

### Row layout details from the references
- Folders: date-only second line; files: "size date" (the Downloads shot
  shows "11.4 KB 11/8/2026"). fmt_date dropped the time everywhere.
- Separators removed (clean white rows in the light reference).
- Properties gained the Description section (Title/Authors/Comments/Tags)
  per file_property.png.

### Multi-layer coloured SVF type icons
files/tools/vec_icons.py authors the W10M type icons as layered VEC2
command streams (back panel / paper sheet / amber front / highlight /
shade for the manila folder; sky+sun+mountains photo, Groove-purple
note, film strip, globe html page, slate phone, gold-contact SD card).
The generator byte-validates every stream against svf.c's scan() before
emitting files_vecicons.h. Row/grid/nav/properties icons now render
through the shared SVF engine (SVFDrawTint, no recolor) — which required
the real scanline r_polygon + integer r_ellipse in files_app.c (they
were no-ops before).

### file:// open-in-Edge (the cool test)
- files: tapping .html/.htm/.md/.txt builds "file://\<device path\>" and
  calls CreateProcessW(\fls0\Apps\edge\edge.exe, url), then PostQuit.
- host: CreateProcessW + GetCommandLineW builtins; main.c registers a
  launch hook that matches the image to a loaded module by basename,
  stashes the command line, re-snaps the shared-component IAT slots to
  the new foreground (fingerkb/shellchrome repaint with edge's dark
  context) and calls its RunApplication; when the launched app exits the
  foreground + context return to the caller.
- edge: nav_url gained the file:// branch (full URL in the address bar,
  \fls0-relative path for the md engine); RunApplication consumes
  GetCommandLineW and opens the document in tab 0. paint_page renders
  .html/.htm through the md engine too. The e2e's edge-open shot shows
  edge's dark chrome + the parsed ntos.html and the log line
  `launch: foreground -> edge.dll (cmdline "file://\fls0\Documents\ntos.html")`
  is asserted.
- device: payload/coredll.c gained the real implementations the files
  model needed: CopyFileW (CreateFile/Read/Write composite),
  RemoveDirectoryW (kernel unlink on the empty dir), GetCommandLineW
  (reads + consumes \fls0\intent.dat) and CreateProcessW (writes
  intent.dat from the command line + coredll_launch hand-off) — the same
  private channel StartHost's next.dat uses. tests/coredll_files_test.c
  covers all four on the host build.

### Smoothness
- layout_doc now runs ONCE per doc (static absolute geometry cached in
  nrect[]) instead of per paint.
- Touch drag scrolls the list directly (WM_MOVE_ with an 8px threshold)
  with a proportional scrollbar thumb — the list finally scrolls in the
  e2e (drag script lines).
- Icon draws go through the clip-checked GDI path; off-screen rows skip
  early (unchanged) and the sort-bar/toast repaints stay single-pass.

### e2e v2 (22 -> 27 frames, all green)
boot / navpane / documents / sortflyout / sortdesc / sortasc /
overflow-browse / props-root / selectmode / overflow-select / rename
(typed "movies" on the SIP) / dest-picker / dest-documents / move-done /
dest-copy / copy-done / scrolled / scrolled-top / edge-open + final.
verify_fs.py asserts the rename/move/copy physically landed on disk.
VLM parity vs LIGHT_THEME_this_device.png: ~85-90% (folder tone matched
to the reference, remaining deltas are icon stroke weight + sample data).
