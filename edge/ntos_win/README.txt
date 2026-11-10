NTOS SH4 — Windows test host (Edge POC and friends)
==================================================

This bundle runs the REAL NTOS payload DLLs — the exact same PE images the
calculator runs — natively on your Windows PC, wrapped in a Wine-like host
that mirrors the WinCE-style COREDLL surface. It is the fast iteration loop
for UI work: click the app, watch it paint, break it, record it.

Quick start
-----------
1. Unzip anywhere. Double-click run-edge.bat (or run from cmd for logs).
2. A window opens showing the 320x528 device LCD. It is live: click, drag
   and type — the app responds exactly like on-device.
3. Everything you do is written to session.script as an e2e script in the
   ntos_linux format. Send that file back and the exact interaction can be
   replayed in CI.

Input mapping
-------------
  left click / drag   taps and scroll drags (WM_DOWN_/WM_MOVE_/WM_UP_)
  keyboard            WM_KEY_ + WM_CHAR_ (the on-screen keyboard works too)
  F12                 screenshot shot-interactive-001.png, -002, ...
  Ctrl+Q or [X]       quit; the final frame is saved as lcd-final.png

Other apps
----------
The exe is app-agnostic; any NTOS payload DLL set works. Ready-made
packages (make package-files / package-cortana / package-calc) mirror
each app's e2e module list and land as ntos-windows-<app>.zip with a
run-<app>.bat launcher; e.g.:

  ntos_windows.exe html.dll fingerkb.dll shellchrome.dll chakra.dll edge.dll --fs fs

Options
-------
  --entry NAME   entry export to call (default RunApplication)
  --fs DIR       directory that plays the device \fls0 storage
  --script FILE  headless deterministic replay (no window input); produces
                 PNGs identical to the Linux host run of the same script
  --shot PREFIX  screenshot name prefix (default lcd)
  --record FILE  where the session is recorded (default session.script)
  --scale 1|2    LCD window scale (default: auto from your screen size)

Debugging artefacts
-------------------
  ntos-win.log     full host log (module loads, IAT snaps, launches)
  session.script   everything you did, as a replayable script
  shot-*.png       F12 screenshots; lcd-final.png on exit

Where things come from
----------------------
  ntos_windows.exe  host (edge/ntos_win in the repo)
  edge.dll          Edge chrome + app (edge/app)
  html.dll          shared HTML4/CSS1/Markdown document engine (payload/phone/markdown.c)
  fingerkb.dll      shared soft-input panel (payload/phone/fingerkb.c)
  shellchrome.dll   shared shell nav bar (payload/phone/shellchrome.c)
  chakra.dll        JS API-shape stub (edge/app/chakra_stub.c)
