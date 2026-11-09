# Critical NTOS filesystem gate and safe application recovery

**Status:** implementation and host-unit tests, **not yet hardware certified**.

## Filesystem invariant

On this CP400 firmware the SDK filesystem must be treated as a
**single-descriptor service**. An application must not have two SDK file
descriptors open simultaneously, even if they reference different files.
A directory enumerator also occupies that same exclusive slot.

NTOS now routes:
- loader/picker EXE opens, dependency DLL/CPL staging and existence checks
- COREDLL file open/read/write/close via the NTOS API table
- WinCE FindFirstFileW/FindNextFileW/FindClose directory searches
- native picker scans, linked app launches and app-switch relaunches

through a single P1 `ntos_fs::Gate`. The gate maintains one typed
handle (file or finder), denies a second open without invoking the SDK,
and refuses any read/seek/close for a foreign handle. Rename, delete,
mkdir and stat are denied while any SDK descriptor is live.

Close failure is **sticky**: it does not release the gate. NTOS tries
once to clean up a leaked handle when a PE exits or fails to link. If
closing still fails, NTOS refuses further filesystem calls instead of
attempting an overlapping open. A successful cleanup increments a
diagnostic counter. The gate is reset only after the SDK confirms
close success, not merely when a synthetic WinCE handle is freed.

In `COREDLL.DLL`, `CloseHandle` and `FindClose` also retain their
synthetic handles if the underlying API close fails. Apps can retry a
close; even if they don't, NTOS's stage0 owner can recover it at the
process boundary.

The main EXE/dependency PE is fully staged in P1 memory and closed
**before** MMU translation is enabled. No filesystem operation occurs
in the VBR exception handler.

### What cannot be guaranteed

This does not recover OS descriptors created directly by code that
bypasses the NTOS file API, nor does it imply safe recovery after an
unhandled CPU exception that prevents the pager trampoline returning.
It also cannot reverse filesystem corruption that has already happened.
Only the actual CP400 emulator and hardware tests can confirm the
underlying SDK's close-result semantics and reliability. Back up
`fls0` before testing new builds on real hardware.

## Install tree

Shared DLLs and CPLs are in exactly **one place**:

```text
NTOS.hh3
STARTHOST.EXE
CONTROL.EXE
CALC.EXE
SVFEDITOR.EXE
Apps/
Windows/
  demo.cpl
  System32/
    COREDLL.DLL
    SVF.DLL
    SETTINGSUI.DLL
    DEMO.CPL
    ...
```

The PE loader first tries `\\fls0\\Windows\\System32\\MODULE.DLL`
(or `.CPL`), and falls back to `\\fls0\\MODULE.DLL` for
existing installations. Both tries are serialized: the second is
attempted only after the first returned without opening a descriptor.
The installer produces **no duplicate DLLs** in the root.

## Application error vs kernel fault

Malformed PE headers, unresolved imports/ordinals and missing DLLs
are **not a kernel crash**. All mapped allocations and any initialized
DLLs are released; the window shows a pale nonfatal
`NTOS - app could not start` screen and returns to the EXE picker
after a fresh key press.

If the MMU exception callback records a **fatal fault and returns to
P1**, NTOS restores MMU/VBR, tries FS cleanup, and displays
`NTOS KERNEL FAULT - SAFE EXIT` with TEA, EXPEVT, SPC, SSR,
and MMUCR. EXE/CLEAR exit the add-in back to the host launcher,
without opening any more files. A hardware exception that never
returns from its ISR cannot use this software dialog; no unsupported
claim of full fault containment is made.

## Browser SVF studio: custom Settings icons

Open `tools/svf_studio.html` in a browser directly from a checked-out
repository (it requires no server, Python, libraries, permissions or
network connection). Draw with rectangle, filled rectangle, line,
circle or arc; choose **100% black** or **50% black**; preview at
17/24/32/48 px; export a `VEC2` .SVF file. Existing supported
VEC1/VEC2 simple shapes can also be imported and edited.

Rename your exported file after the Settings icon it should replace,
for example `system.svf` or `devices.svf`. Copy it to
`payload/phone/custom_settings/system.svf`. `make -j4` checks these
overrides before using the built-in generated icon. The generated
fallback remains in `payload/phone/assets/settings/` and can always
be restored by deleting the override. Supported override names:

```text
system devices network personal accounts time gaming privacy
settings display notifications phone message storage battery
maps about update
```

These are raw embedded SVF assets. No change to any WinCE/DLL export
is needed. Source-comparison utility:
`python3 tools/svf_icon_preflight.py --all --root dist`.

## Regression tests

`tests/fs_guard_test.cpp` checks same-file and different-file
double-open denial, finder/file exclusivity, sticky failed close,
successful recovery on exit, and eventual handle reuse.
`tools/check_fs_guard_wiring.py` asserts no raw SDK File_Open calls
were introduced outside the exclusive bridge and verifies System32
packaging. CI remains split between one calculator-ready Build and
independent Tests and CodeQL.
