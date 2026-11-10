# Cortana port worklog

One big log, newest entries at the bottom. Date format YYYY-MM-DD.
Same workflow as edge/WORKLOG.md and files/WORKLOG.md.

## 2026-11-10 — session 1: package triage + the demo app, e2e green

### Reference artifact: Microsoft.Windows.Cortana_cw5n1h2txyewy.zip
RS1 Cortana: SearchUI.exe 7.1MB + 51 loose Views/*.xbf pages + a 3.4MB
resources.pri, on top of a large service fabric (CortanaApi, Signals,
node.dll, 292 grammar files) that is meaningless without a network —
which NTOS does not have, and neither did the reference phone in flight
mode (the captures show the offline states).

### Analysis
- files/tools/pri_decode.py decodes the pri untouched: 1253 resources,
  1246 en-US values -> cortana/analysis/pri_out/. The demo's ground
  truth strings came straight from it: "How can I help?"
  (greetingsresources/Fallback_Text_8), "Go ahead and tell me to do
  something. Interests aren't available at the moment.", "Thinking...",
  "I can't connect at the moment. / Make sure you are currently
  connected to the internet."
- refs/cortana/*.png pixel-measured: page #F7F3F7, orb at (160,48) d48
  (halo + thick #4A71FF ring with the lower-right gap), greeting #3E6BE0
  at y84, 3-line 20px body from y138, search band #DEDBDE y432..480,
  44px #0078D7 mic square. Written up in analysis/CORTANA_UI_REFERENCE.md.

### The app (cortana/app)
- cortana_app.c on the shared skeleton (uxaml.c engine, uixctl + svf
  painters, FINGERKB.DLL keyboard, files_icons glyphs — FI_MIC added).
- The orb is a layered VEC2 asset (tools/vec_cortana.py): white field +
  halo arc + ring arc with the gap, rendered through the shared SVF
  engine; the "thinking" variant rotates the gap wider. Two build
  lessons in the worklog below.
- States: Home -> Input (SIP + floating query field, the files-app
  search pattern) -> Thinking ("Thinking..." strip + X) -> the offline
  error page. Music button lands on the MusicReco network error.
- e2e (6 frames): home / input ("weather" typed on the shared SIP) /
  thinking / error / home-again / final — palette + orb + greeting +
  band + mic anchors, all green.

### Two first-run bugs (both caught by the e2e's pixel checks)
1. The orb painted NOTHING: VEC arcs stroke with the SETSTYLE *stroke*
   colour; authoring the ring as a fill made every arc invisible.
2. The greeting text drew off-screen: root-level centred text needs
   x=0 w=320 — the uXAML layout inherits the parent's width when w is
   unset, so x=160 + center-align lands the pen at x=320.

### Next
- Real-device pass over the orb gradient (the reference ring carries a
  subtle blue gradient; the VEC stroke model is flat).
- COOBE consent page (ask.png) + hamburger notebook menu if the demo
  grows; SH4 device build through build_phone.py / elf2ntx.

## v2 — system navigation bar (user feedback round)

- cortana now imports the SHELLCHROME.DLL nav bar through
  UIXChromeFactory — the same hosted-app surface edge, settings and
  files use — themed light through two new cort_theme slots (NAV_BG
  #CECFCE / NAV_INK #313031; cortana_light.png shows the strip under
  the light home and error pages).
- Painted topmost on the light surfaces (home / thinking / error); the
  ask state keeps the SIP edge-to-edge like ask.png, no strip.
- Input: Back mirrors the hardware back key (back from home exits),
  Home resets to the light home, Search opens the ask state — all
  covered by the e2e (nav-search / nav-home frames).
- e2e: 6 -> 8 frames, checker asserts the navbar strip + Back glyph on
  every light frame; shellchrome.dll joins the module load list.

## 2026-11-11 — native test package
- edge/ntos_win `package-cortana` assembles ntos-windows-cortana.zip
  (fingerkb + shellchrome + cortana, run-cortana.bat). files-ci builds
  it and runs the wine cross-host parity gate on cortana.script.
