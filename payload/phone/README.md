# Native phone shell: SH4 BE, WinCE-first

Build: `make phone`. Verify: `make phone-check`. Native visual tests:
`python payload/phone/preview_phone.py` (host GCC + pefile).

Copy **the contents of `dist/phone-device/`, including Apps and home.dat**
to the calculator fls0 root. Use the included **new NTOS.hh3 and COREDLL**:
this revision appends directory-enumeration call gates to the NTOS API.
Do not combine the new DLLs with the older launcher.

## Included

* Separate StartHost, StartMenu, UIXMobile, UIXn, UIXRender, ShellControls,
  ShellChrome, FingerKB, SVF and AppCatalog modules; COREDLL and NTOS.hh3.
* 24 real SH4 BE stub EXEs under Apps/<app>/<app>.exe, each with an embedded
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
Start, a tap selects; a second tap or Enter activates. This avoids launching
on pen-down before a drag, since the existing input bridge has no pen-up.
Use the app page buttons to pin/unpin or switch small/medium tile sizes.
Up/down scroll, right opens All Apps, left goes back. CLEAR exits.

The app pages are intentional stubs sharing the host UI. Opening them in
Start does not create a new process. Each generated EXE also has its own
entry and app ID, so running it through the PE loader opens its matching
stub page. There is no full process manager or real browser here.

## Discovery and icon precedence

AppCatalog enumerates `\fls0\Apps` recursively (two subfolder levels,
128 entries per directory, 40 apps total). It discovers .exe files rather
than a compiled-in app list. It parses the sidecar first, avoiding PE reads
on the normal path. Missing/invalid/no-icon sidecars fall back to:

1. Embedded SVF: PE RT_RCDATA (10), first numeric resource; generated apps
   use ID 1, language 1033.
2. PE RT_ICON (3): small uncompressed 32-bit DIB, up to 32x32, bounded to
   4608 bytes. PNG-compressed, paletted and large ICO resources fall back.
3. First letter of the app name.

Discovery occurs when StartMenu initializes. Restart StartHost to pick up
new folders or edited sidecars. Files with duplicate app IDs are skipped.

`generate_apps.py` contains the sample catalog/icons; build_phone.py creates
all EXEs, resources and sidecars in one batch. It preserves an existing
home.dat instead of resetting user pin edits. The adjacent .svf files are
editable authoring copies; changing them alone does not update the manifest.
Use write_manifest.py to install one of your cleaner svf.py icons:

```
python payload/phone/write_manifest.py dist/phone-device/Apps/edge/edge.exe "Microsoft Edge" my-edge.svf --id 11 --color 3960ff
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
