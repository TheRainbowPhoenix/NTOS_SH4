# Unified NTOS build audit — 9 November 2026

## Observed emulator baseline

The user has verified `ABITYPED.EXE=0xAB1C0421`,
`ABISTRESS.EXE=0xAB1C0600`, and
`COREDLLABI.EXE=0xAB1C0500`.
These validate the specific generated MSVC SH4 interface fixtures,
not the ABI of every exported phone, WinCE or app-specific function.

## One deployable *native* phone + WinCE-style bundle

```sh
make unified
```

`dist/` includes the full normal native PE payload set
(`CALC.EXE`, `METROWIN.EXE` and others), the 24-phone-app tree, a single native `COREDLL.DLL` and `CONTROL.EXE`.
The standard default loader remains eager-resident; `dist_pager/NTOS.hh3`
is an opt-in test build while exception delivery is not qualified.
The old `phone-device/` staging directory is eliminated. `tools/check_unified_bundle.py`\nchecks one flat install root, 24 phone apps, and native Control Panel imports.

**Use the separately built experimental pager HH3 when testing graphs exceeding 55 UTLB translation slots.** The normal `dist/NTOS.hh3` retains its
safe eager-page rejection policy. The pager handles translation
replacement while all module backing stays in RAM; it does not
implement file-backed page eviction or full gint hardware ownership.
Unhinted fault delivery has not passed the user's emulator probe and the pager\nis not qualified for physical ClassPad deployment.

The original phone and native WinCE-style programs share the same
native C COREDLL, not a fully Microsoft ABI COREDLL.

## Separate original WinCE ordinal ABI (NOT YET ONE COREDLL)

`python3 payload/wince/build_compat.py` now also builds directly
inside the SH4 CI container; it emits the **different**
`dist/ce-demo/COREDLL.DLL`, `COMMCTRL.DLL`, and
`Windows/demo.cpl`. This profile contains the partially implemented
original CE ordinal services and MSVC-facing adapters. CI deliberately
asserts that its COREDLL image differs from the native phone COREDLL:
silently installing one over the other is **not safe**.

A correct unified MSVC-facing COREDLL needs one export address table
containing the original CE ordinals, plus distinct, explicitly typed
native-phone aliases (or rewritten phone IATs). The existing phone
modules currently call GCC-native IAT functions; applying a
Microsoft ABI to the DLL alone would corrupt stack/aggregate calls.
Likewise CE app imports by ordinal must resolve to Microsoft ABI
entries, **not** the native function bodies. Some CE functions
depend on incomplete services and `COMMCTRL.DLL` behavior.

Until this is implemented and emulator-tested, **do not claim that
phone + original CE binaries run together under a single MSVC ABI**.
Keep the full CE demo bundle separate when testing original CONTROL.
The normal phone/pager build is not being silently replaced.

## Calculator and WinCE navigation

The new default `CONTROL.EXE` is a native WinCE-style Control Panel recreation,\nnot the original converted Microsoft PE. Its File/View strip is interactive,\nbut the reference applet and registry semantics are not implemented.\n\nThe pre-existing freestanding `CALC.EXE` now has a classic
File/Edit/View top menu with working Exit, Clear and view toggle,
and its operations remain FPU-free. The screenshot and uploaded
`calc.exe.c` guide the look and intended app interactions; this
is a native recreation, not a reassembled MSVC original.

The isolated CE `windows.c` paint path now overlays a small
File/View strip over the Control Panel top-level chrome and repaints
the existing optional bottom Back/Home/Switch navigation. The
original resource-driven WinCE menu manager, popup commands,
registry applets and touch flows are **not fully implemented**.
The menu is a minimal interaction experiment pending emulator visuals.
Native COREDLL already supports `\\fls0\\nav.dat` toggling its
soft bar and serial handoff to `STARTHOST.EXE`.

## C/H style, static checks and CI

Clang-format (pinned to the SH4 v2.2.2 Debian container toolchain)
normalized **82 authored C/H files**, including native, phone and
WinCE implementations. Generated bitmap/icon headers, third-party
PE fixtures and original shcl compiler-comparison sources are
intentionally excluded. `tools/format_c_sources.py --check`
enforces formatting. CI also runs Clang's static analyzer through
`clang-tidy` on representative C entry points; it is not a claim
that every target-specific translation unit can be host-analyzed.

CI builds the standard NTOS and pager variants, both experimental
MSVC ABI packages, phone and WinCE CE-profile outputs, plus the
native unified bundle. Compilation is not proof of runtime
compatibility across *all* original CE DLLs.

## Next integration gate

1. Validate `dist/NTOS.hh3` with `STARTHOST.EXE` and
   `CALC.EXE` in the emulator, then navigate Home/Back.
2. Validate the CE Control Panel's new File/View strip and bottom
   navigation separately in the CE profile.
3. Build a single COREDLL ELF carrying *both* CE ordinal exports
   and explicitly named native-phone export aliases; avoid
   duplicate export names and ordinal collisions.
4. Retarget/bridge every phone import using its declared signature;
   verify WinCE named/ordinal imports with real MSVC ABI fixtures,
   then add runtime cross-app switching tests before promoting
   the merged core as the default everywhere.
