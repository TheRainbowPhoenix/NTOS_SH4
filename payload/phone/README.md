# Native phone shell: SH4 BE, WinCE-first

Build: `make -j4` (or `make phone`). Verify: `make phone-check`. Native visual tests:
`python payload/phone/preview_phone.py` (host GCC + pefile).

Copy **the contents of `dist/`, including Apps and home.dat**
to the calculator fls0 root. Use the included **new NTOS.hh3 and COREDLL**:
this revision requires NTOS 0.4, Phone API 1 / 28 gates (finder + app handoff).
Do not combine the new DLLs with the older launcher.

## Included

* Separate StartHost, StartMenu, UIXMobile, UIXn, UIXRender, ShellControls,
  ShellChrome, FingerKB, SVF, AppCatalog and SettingsUI modules; COREDLL and NTOS.hh3.
* 24 SH4 BE app EXEs (a working Calculator plus 23 placeholders) under Apps/<app>/<app>.exe, each with an embedded
  SVF resource and a fast binary <app>.exe.manifest. All apps, including
  Microsoft Edge, have icons; only News has a content tile.
* Folder-driven app list, alphabetical sorting/search, black letter picker,
  icon home tiles, 1x1/2x2 pin sizes and persistent home.dat preferences.
* Lowercase, Shift/Caps, numbers and alternate symbols keyboards. Euro,
  pound and yen keys produce Unicode. Emoji is intentionally omitted.
* Pull-down quick settings with SVF icons, local demo toggles and dismissible
  local notifications. Radio/connectivity toggles do not control hardware.

No float math, alpha compositing, XML, TTF parser, photographs or second
framebuffer on device. Fixed-size bitmap glyphs, integer SVF paths and GDI
fills. The catalog holds at most 40 apps with 512-byte SVF icon caches.

## Interaction

Tap Search to show FingerKB; typing filters the app list. The white search
field and keyboard geometry follow the reference screenshots (keyboard
starts at y=271). Shift cycles normal/shift/caps; &123 opens numbers;
its left arrow key switches the alternate symbol page; abc returns.
Backspace, comma, space, period and Search/Enter work. Back hides the SIP.

Drag down from the top status bar to open notifications; swipe up or tap
Collapse/bottom handle to close. Tap a quick tile to toggle its demo state.
All settings opens the Settings stub. Tap Clear all to dismiss notifications.

Tap a group letter for the solid-black alphabet picker. In the list or on
Start, releasing a tap launches the actual discovered EXE. Moving more than
8 pixels cancels activation and scrolls; holding never opens tile editing.
F3 opens explicit pin/size controls for the selected tile. Up/down select
and scroll to the selection; EXE launches it. Right opens All Apps; left or
physical Clear goes back.
Physical Keyboard toggles FingerKB (mapped to VK_F2); Clear no longer quits
the app through WM_QUIT. Back returns from an app to Start.

Activation queues the selected EXE path through a capability-checked private
COREDLL gate, then quits StartHost. NTOS unloads all modules before loading
the app, keeping only one app resident. Clear exits a standalone app back
to StartHost. Calculator uses the existing working CALC implementation;
Settings opens its native Settings/System/Display pages. Other apps remain
placeholders. This is serial app switching, without a concurrent process manager.

## Discovery and icon precedence

AppCatalog enumerates `\fls0\Apps` recursively (two subfolder levels,
128 entries per directory, 40 apps total). It discovers .exe files rather
than a compiled-in app list. It parses the binary sidecar for app metadata and caches a valid adjacent
<app>.svf in preference to its embedded sidecar icon. Missing icons fall back to:

1. Embedded SVF: PE RT_RCDATA (10), first numeric resource; generated apps
   use ID 1, language 1033.
2. PE RT_ICON (3): small uncompressed 32-bit DIB, up to 32x32, bounded to
   4608 bytes. PNG-compressed, paletted and large ICO resources fall back.
3. First letter of the app name.

Discovery occurs when StartMenu initializes. Use Refresh on Start to pick up
new folders or edited sidecars. Files with duplicate app IDs are skipped.

`generate_apps.py` contains the sample catalog/icons; build_phone.py creates
all EXEs, resources and sidecars in one batch. Each build refreshes the
shipping home.dat to the mixed 1x1/2x2 layout, saving changed preferences in
home-backup.dat. On device, missing or legacy uniformly-medium layouts load
home-default.dat; existing mixed user layouts stay intact. Adjacent .svf
files are live overrides: edit one and use Refresh on Start to see the change.
Use write_manifest.py to install one of your cleaner svf.py icons:

```
python payload/phone/write_manifest.py dist/Apps/edge/edge.exe "Microsoft Edge" my-edge.svf --id 11 --color 3960ff
```

## Binary formats

All scalar fields below are **big-endian**, independent of PE headers.

PAM1 sidecar (16-byte header, then UTF-8 name, then raw VEC1/VEC2 stream):

| Offset | Field |
|---|---|
| 0 | 4 bytes PAM1 |
| 4 | u16 stable app ID |
| 6 | u8 flags: bit 0 content/live tile (News demo) |
| 7 | u8 default size: 1 or 2 |
| 8 | u32 WinCE COLORREF (00BBGGRR) |
| 12 | u8 UTF-8 name length, 1..39 |
| 13 | reserved zero |
| 14 | u16 SVF length, 0..512 |

PIN1 home.dat: magic PIN1, u16 count, two reserved bytes, then count 8-byte
records: u16 app ID, u8 size, u8 grid x, u8 grid y, three reserved bytes.
Valid saved grid positions are retained. Overlapping/new tiles are packed
automatically from record order and sizes when loaded. Unknown app IDs are ignored. Pin buttons write through WinCE file
APIs. CREATE_ALWAYS was fixed to replace old file content, including when
unpinning makes the preferences file shorter.

SVF.DLL exposes SVFValidate and SVFDraw. It follows svf.py: VEC1/VEC2,
BE signed int16 coordinates, RGB555 and 0xffff transparency, viewbox,
rect/bordered rect, circle/ellipse, lines, polygons (up to 255 vertices),
text hints and object-name metadata. Coordinates scale with integer math.
Malformed/truncated commands are rejected before drawing; streams are
bounded to 4096 bytes/512 commands. Cached app icons are capped at 512 bytes.
Text hints use a compact fixed font. Unknown VEC2 opcodes are rejected.

## WinCE keyboard boundary

The original phone ShellChrome exports SetSipVisibility (Ghidra
/recreate/WinPhone/10/ShellChrome.dll, 10041df0); FingerKB provides the UIX
keyboard resources. The CE shell reference PRIVATE/SHELL/SHELLPSL/mysip.cpp
confirms SipShowIM, SipGetInfo and SipSetInfo as the light platform boundary.
This implementation is independent C, without copied CE/NT source.

COREDLL now implements SipShowIM(0/1), SipGetInfo and SipSetInfo with the
WinCE SIPINFO layout, SIPF_ON/SIPF_DOCKED and a visible-desktop rectangle.
Search focus explicitly requests SIP visibility; leaving it hides the SIP.
UIXn paints FingerKB and converts its key results to WM_CHAR/WM_KEYDOWN for
the active view. ShellChrome uses that same SIP state. This is the reduced
native profile, not a complete Windows Text Services/IME implementation.

## Build on Windows with the minimal HH3 image

The v2.2.1 image has the compiler but no Python. From the repo root:

```powershell
python payload/phone/generate_apps.py
docker run --rm -v "${PWD}:/work" -w /work ghcr.io/classpaddev/hollyhock-3:v2.2.1 sh -c 'export PATH=/toolchain/bin:$PATH; sh payload/phone/compile.sh && make -j4 elf hh3 obj/payload/coredll.elf'
python tools/elf2ntx.py obj/payload/coredll.elf -o dist/COREDLL.DLL --base 0x10000000 --dll --exports coredll_puts,coredll_getkey,coredll_refresh,coredll_malloc,coredll_free --exports-file tools/coredll_exports.txt --stub-symbol coredll_stub --dll-name COREDLL.DLL
python payload/phone/build_phone.py --pack-only
python payload/phone/check_phone.py
python payload/phone/preview_phone.py
```

## Compatibility and checks

All DLLs share preferred base 0x10000000 and have real BE relocation sites.
abi/*.json preserves original named and ordinal-only export identities.
phone-build.json lists implemented/stubbed exports. Most Windows Phone
renderer/shell services remain E_NOTIMPL stubs, and phone.h context/schema
tables are a private small profile, not Microsoft's COM ABI or UIB parser.
The original three application method names are retained. Desktop startup,
CRT/ETW/SEH and full transitive Windows imports are not reproduced.

Checks cover final PE machine, export parity, import resolution, BE
relocations, embedded resources on all 24 EXEs, catalog discovery, malformed
SVF rejection, sidecar/embedded-SVF/DIB/letter fallbacks, pin/unpin/resize
persistence across reload, keyboard pages, search, gestures, notifications
and the native C application lifecycle. Tests use an isolated filesystem,
not the shipping preferences. Native PNGs are generated by the same C
renderer and RGB565 rules; actual SH4/MMU/device execution remains untested.

## Launcher compatibility check (NTOS 0.3)

The selection screen must show NTOS 0.3 and Phone API 1 / 27 gates.
The regular, pager and verification launchers have all been rebuilt with
versioned API-window metadata. New COREDLL finder functions reject a
launcher without the directory gates with ERROR_NOT_SUPPORTED.

The previous dist_pager image had only 24 API entries. Calling slot 25
read adjacent SDK fixup data: 0x8cff5a38, the Battery_GetVoltage string.
Its initial bytes 42 61 caused the reported invalid instruction 0x4261.
This was an unchecked launcher/runtime ABI mismatch.

Regression checks: `python tools/check_ntapi_table.py`; compile/run
`gcc -std=c11 -Ipayload tests/ntapi_caps_test.c -o obj/phone-host/ntapi-caps-test.exe`.

## Settings placeholder and mixed tiles

Settings.exe (app ID 21) now enters SETTINGSUI.DLL: Settings categories,
System options, then Display. Brightness, automatic brightness, rotation
lock and display scale have local preview state; Apply saves that state
and Cancel restores it. No hardware display settings are changed.
Other categories remain placeholders. Settings search accepts keyboard
input. Clear backs through Display -> System -> Settings -> Home.

Generated Settings SVFs are in payload/phone/assets/settings; normal app
SVFs are next to their installed EXEs. The default Start arrangement now
mixes small and medium tiles throughout, including a pinned Settings tile.
Existing custom home.dat pin layouts are preserved. home-default.dat is
always generated as the latest default layout; copy it to home.dat to
reset a device whose pins you already customized.

GitHub C/C++ CI builds the normal distribution (make -> ntx -> phone),
checks that all 24 app EXEs and SETTINGSUI.DLL exist, validates the launcher
API tables and PE dependencies, and runs the actual C Settings/input
previews on Linux. dist/** includes the flat Apps tree.
These workflow changes take effect after committing/pushing them.

## clemu VFS compatibility

COREDLL keeps a bounded table of complete SDK file descriptors. Fugue uses
mount-tagged descriptors such as 0x00010001; they must not be truncated or
rejected as oversized Win32 handles. This applies to preferences, app manifests,
SVF icons and nested app files. The actual bridge regression is
`tests/coredll_files_test.c` (run by CI).

Calculator has an X close button in its title bar. Clear/ESC and firmware
action-bar Back also close/go back. A drag permanently cancels a pending
app activation for that contact, including repeated DOWN or a moved release
without intermediate MOVE events.

The clemu `ntos_launch.py` seeder must copy the complete `dist`
tree, including Apps and home-default.dat; root-level EXE/DLL copying alone
is insufficient. Existing on-device home.dat is retained by seeding.

## Catalog cache and placeholder pages

`apps.dat` is a portable big-endian CAT1 catalog, separate from PIN1 home.dat.
It caches each app ID, name, executable path, colour, size, flags and SVF icon.
The sample build generates it up front. Normal startup reads apps.dat and
home.dat only (two file opens, no folder enumeration or per-app metadata reads).
A missing/truncated/invalid cache is rebuilt from Apps using bounded, sequential
file access. Tap Refresh below the Start tiles, press F5 in Start, or delete apps.dat to refresh after installing
apps or editing sidecars/icons. Deployment regenerates the sample cache.

CAT1: magic, u16 count, u8 version=1, u8 reserved=0. Each record is 24 bytes:
u16 ID, u8 flags, u8 size, u32 COLORREF, u8 name length, u8 path length,
u16 SVF length, u32 PE icon offset, u32 PE icon size, u8 PE icon type,
three reserved bytes; then name, path and SVF bytes without NUL terminators.
Reads/writes are buffered in 4096-byte chunks, sharing the existing work buffer.

Placeholder EXEs show a dark title bar with their SVF icon and app name,
and the name as a page heading. Clear returns to Start. News retains its demo.
Fugue paths are normalized to exactly one leading slash; adding a second
creates an empty volume name and ENOVOLUME, unrelated to open-handle exhaustion.

## Settings, file list and exit controls

Settings scrolls to Update & security; Phone update, Device encryption and
For developers are local preview pages. Gear always returns to Settings root.
Display uses the same bitmap face for upper/lowercase letters and square toggles.
None of these preview settings changes hardware, encrypts storage or installs updates.

File Explorer browses fls0 with folders first, up to 48 ASCII entries per folder.
Tap folders to enter; tap EXEs to launch; arrows select and EXE opens. Clear goes
up, then closes the app at the volume root. The browser does not modify files.

Clear goes back in the phone UI and exits at app/Start root. Other WinCE apps
receive WM_QUIT for physical Clear. Notifications dismiss on a short upward
swipe, Collapse, or the bottom handle; the Exit quick tile quits the current
EXE. From Start it returns to NTOS; Clear or Left then exits NTOS.hh3.

## Optional phone navigation and Excel demo

Back, Windows/Home and Search occupy y=488..527 by default. Back is an app
navigation signal: hide FingerKB first, then go up a Files/Settings level or
leave the app. Home queues a fresh STARTHOST.EXE through the serial loader.
Search opens the app list/search, including from Excel and legacy Calculator.
The bar also overlays windowed WinCE apps through COREDLL after EndPaint.

Toggle the Soft keys quick-settings tile, or F4 in a phone UI, to show/hide
the bar. The preference is one byte (0 or 1) in nav.dat; absence means enabled.
FingerKB moves up by the reserved height, and the list/file/settings viewports
scroll above the bar. Legacy app Search uses a one-shot next.dat intent that
Start consumes and deletes. All DLLs in this build must be deployed together
because the private UI table extensions include viewport/docking callbacks.

Excel is a UI demonstration, not a workbook parser or calculation engine.
Tap the green Office/Excel preload screen or press EXE to open sample data.
The top-left menu switches Employee/Project samples; rows scroll and cells
can be selected. Back/Home/Search remain available on the splash and sheet.

Notification handle gestures accept TOUCH_ACT_BAR starts and also evaluate
upward movement on release when intermediate moves are absent. Closing the
shade returns a repaint signal and consumes the remaining gesture.
