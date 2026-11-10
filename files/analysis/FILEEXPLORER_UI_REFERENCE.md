# FileExplorer (W10M) UI reference — XAML-derived spec

Source: `Microsoft.Windows.FileExplorer_cw5n1h2txyewy.zip` (10.0.14393.0,
RS1, built 2016-07-16 — same branch day as the Edge 38.14393 package).

Unlike Edge (whose XBF pages were stripped from the shipped pri), this
package embeds **all 17 compiled XAML pages as XBF v2 blobs** inside
`resources.pri`. `files/tools/pri_decode.py` + `pri_extract_files.py` +
`xbf_dump.py` decode them; the object trees live in
`files/analysis/xbf_dump/*.txt`, every en-US display string in
`files/analysis/pri_en_us.txt`. This document is the port spec built from
those trees + the W10M design language. Real-device captures will refine
pixel values later (same workflow as `edge/analysis/EDGE_UI_REFERENCE.md`).

## Architecture (from app.xbf / MainPage.xbf / MainFrame.xbf)

```
App (app.xbf)
 └─ Merged dictionaries: DesignCore.xaml (27KB style lib) + View/Colors.xaml
 └─ MainFrameTemplate -> MainFrame (NavigationThemeTransition)
     └─ MainPage (BasePage), grid rows [Auto][*][Auto]:
         ├─ NavBar            (top, 48px):  hamburger(48) + AddressBar + UpButton(48)
         ├─ ExplorerBrowser   (FolderViewPage -> FolderView -> FolderViewListView)
         └─ FrameAppBar       (bottom, 48px): AppBarButtons + "..." overflow
         └─ SplitView pane:   NavPane (overlay, 256px) behind a light-dismiss scrim
Visual states: Portrait (phone: pane closed, hamburger visible) / WidePortrait /
PortraitWithNavPane / WidePortraitWithNavPane / Landscape
```

## Pages (all decoded, files/analysis/xbf_dump/)

| XBF page | Role | Key elements (exact) |
|---|---|---|
| MainPage | shell | hamburger `MenuIconButtonStyle` Z500, SplitView `SmallSplitViewOpenPaneLength`=256, rows Auto/*/Auto, `MenuButtonOffset`=48,0,0,0 |
| NavBar | top bar | Border `NavBarBackgroundThemeBrush` (= `SystemChromeMediumColor`), Grid[*][Auto]: AddressBar + UpButton 48x48 `IconButtonStyle` `SubtitleIconSize` glyph U+E752 |
| AddressBar | breadcrumbs | `AddressBarButton` style: 48px tall, transparent bg, content pad 5,0,0,0, left pad 7, right pad 12, chevron (MDL2, size 8, pad 5,3,3,0) + collapsed "..." ellipsis; AddressBarPanel horizontal, clipped |
| NavPane | hamburger menu | rows: 48px title + list; `NavPane_ListView` 18.666px SemiLight, `SingleLine TouchOptimized` items, MDL2 icon 16px `SplitViewIconMargin` 4,0,16,0; title "FILE EXPLORER" (NavPane_Title, all-caps) |
| FolderViewPage | content page | Grid[Auto][*]: indeterminate ProgressBar (top) + FolderView |
| FolderView | file list | `FolderViewListView` (ItemsWrapGrid horizontal), header = StatusBarTextBlock (margin 12,11,0,13, Body 15) + `SortByTextFilterButton` (12,0,0,0) + EmptyPageContent; Tile ListViewItem = `CheckMode Inline`, Thumbnail = `CheckMode Overlay` |
| CommandsDialog | "..." commands | ContentDialog `FullSizeDesired`, CommandsTitle (margin 0,12,0,20, 17.333 Light) + list of CommandItems (17.333 SemiLight, CharacterEllipsis) |
| RenameDialog | rename | ContentDialog: title 20px (Subtitle) margin 0,12; "File name"/"Folder name" 15px pad 0,0,0,8; TextBox; OK/Cancel |
| NewFolderDialog | new folder | same shape, "Create a new folder" / "Folder name" / "New folder" |
| PropertiesPage | properties | header Grid 0,24: 96x96 thumbnail + ItemName (18px, wrap 2); rows 13px 2-col (label `SystemControlPageTextBaseMedium` / value): Date modified / File type / File size; folder: Folder size + Contains; drive: UsedSpace/FreeSpace (13 CaptionAlt) + 6px ProgressBar max 1.0 |
| ProgressBarDialog | file ops | ProgressBar_Text 17.333 SemiLight + indeterminate bar |
| PickerPage | open/save picker | picker mode of the shell (fileOpenPicker/fileSavePicker extension) |
| SearchEditBox | search | TextBox `InputScope=Search` Margin 12, placeholder via `PlaceholderLabel`, slide-in from top (`PaneThemeTransition Edge=Top`), `TextChangedDelayTimerMs`=100 |
| Colors | theme | NavBar bg = `SystemChromeMediumColor` (light theme = #E6E6E6) |
| DesignCore | shared lib | full W10M type ramp + icon sizes + ListViewItem templates |

## Type ramp (DesignCore.xbf + pri overrides; Segoe UI / effective px)

| Token | px | Used for |
|---|---|---|
| CaptionAlt | 13 | properties rows, UsedSpace/FreeSpace |
| Body/Base | 15 | status bar, breadcrumb buttons, dialog labels |
| CommandItem | 17.333 SemiLight | commands dialog rows, progress text |
| Tile_Secondary | 16 SemiLight | list secondary line |
| Tile_Primary | 17.333 | list primary line |
| NavPane | 18.666 SemiLight | nav pane items + title |
| ItemName_Display | 18 | properties name |
| Subtitle | 20 | dialog titles |
| icons | 12/16/20/24/32/48 | caption/body/subtitle/title/subheader/header |

Bitmap-font mapping on the 320x528 LCD (same effective width as a W10M
phone): 13→12, 15→14, 16/17→18, 18/20→18/22 (baked sizes 9/11/12/14/18/22).

## Palette (W10M **light** theme; Colors.xaml: NavBar = SystemChromeMedium)

| Role | Value |
|---|---|
| Page (AltHigh) | `#FFFFFF` |
| NavBar / AppBar (ChromeMedium) | `#E6E6E6` |
| NavPane surface (ChromeLow) | `#F2F2F2` |
| Ink (BaseHigh) | `#000000` |
| Secondary text (BaseMedium) | `#767676` |
| Disabled/placeholder | `#9A9A9A` |
| Separator | `#E6E6E6` |
| Accent (SystemAccentColor) | `#0078D7` |
| Selected row (HighlightListAccentLow) | `#CCE8FF` |
| Checkbox border (BaseMediumHigh) | `#4A4A4A` → measured `#666666` |
| Scrim | `#7F7F7F` |
| Folder icon fill | `#FFB900` body / `#E8A000` shade (Folder_Large.png amber) |

## Display strings (files/analysis/pri_en_us.txt, en-US ground truth)

Commands: `Copy to` `Move to` `Delete` `Rename` `Properties` `New folder`
`Select all` `Select` `Clear selection` `Search` `Refresh` `Share` `Icons`
`List` `Remove from recent` · picker: `Done` `Cancel` `OK` ·
status: `%1!u! item selected` `%1!u! items selected` `Choose a file(s)`
`Choose a folder` `Choose where to save %1!s!` `This folder is empty.`
`You haven't opened any files recently.` `Sort by: %1!s!` ·
dialog titles: `Rename file` `Rename folder` `Create a new folder`
`Delete` (error) `Not enough space` `Problem with shortcut` ·
properties: `Date modified:` `File type:` `File size:` `Folder size:`
`Contains:` `%1!u! files, %2!u! folders` `used` `free` ·
errors: `Something went wrong. Could not %s the file(s) or folder(s).`
`File names can't contain: \ / :  * ? " < > |` · title: `File Explorer`.

Locations (NavPane, from exe model strings): This device (PC CLSID),
Downloads, Documents, Pictures, Music, Videos, Recent (CLSID), SD card.

## Port geometry (320x528, this POC)

- NavBar y0..47: hamburger 48x48 | breadcrumbs (15px, chevrons) | Up 48x48 (U+E752 up glyph)
- Content y48..479: header (status 15px @ 12,11 / Sort-by filter button /
  empty content) + item area; list rows 56px (icon 36 + name 18px + secondary
  12px gray); icons view 2 cols x 96px (icon 48 + label 12px x2 lines);
  select-mode checkbox 20px inline left (list) / overlay top-right (icons)
- AppBar y480..527: 4 x 80px buttons (icon 18 y7..25 + label 11px y31),
  Select / New folder / Search / "..." ; select mode: Select all / Copy to /
  Move to / Delete
- NavPane overlay: 256px ChromeLow + scrim; title row 48px + 48px rows,
  icon 16 + label 18px SemiLight
- Dialogs: bottom sheets (ContentDialog FullSizeDesired): scrim, white
  sheet, title 20px (0,12), field 32px, button row 48px (OK accent right)
- Properties: NavBar("Properties") + 96px thumb + name 18 + 13px rows at
  12px pitch; drive root adds Used/Free row + 6px ProgressBar (x12..308)
- Search: nav row -> [back 48][field][X], keyboard (FINGERKB.DLL) below,
  live filter after 100ms-equivalent (per-keystroke on host)
- File ops actually mutate the \fls0\ tree (host --fs dir):
  CreateDirectoryW/MoveFileW/CopyFileW/RemoveDirectoryW/DeleteFileW

## Reused shared components (the W10M DLL model)

- `uxaml.c/h` + `uxaml_gen.py` — same uXAML engine as the Edge POC
- `FINGERKB.DLL` (payload/phone/fingerkb.c) — keyboard DLL, themed light via
  the UIX_TH_* table published by this app's Render vtable
- `payload/phone/uixctl.c` — shared button/checkbox/toggle/field painters
  (ctl_*), compiled into files.dll like edge.dll does
