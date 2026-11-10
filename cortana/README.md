# NTOS cortana — Cortana (W10M) demo port

Port of `Microsoft.Windows.Cortana_cw5n1h2txyewy` (Windows 10 Mobile RS1)
to NTOS with the tiny uXAML engine — the "cool demo" app: the light
"How can I help?" home with the layered-VEC orb, the shared-SIP query
flow, the "Thinking..." state and the honest offline error. Follows the
edge/ and files/ POC model (shared markup engine, shared FINGERKB.DLL
keyboard, shared uixctl/svf painters, files_icons glyph set).

## Layout

- `analysis/`
  - `CORTANA_UI_REFERENCE.md` — the port spec (states, palette, geometry)
  - `pri_out/` — full mrm_pri2 decode of the 3.4 MB resources.pri
    (1253 resources, en-US values with the ground-truth strings)
- `tools/`
  - `vec_cortana.py` — the Cortana orb as layered VEC2 assets (home +
    thinking variants), byte-validated against svf.c's scan()
- `app/`
  - `markup/*.xml` — HomePage / ThinkingPage / ErrorPage (compiled by the
    shared `edge/app/uxaml_gen.py` into `cortana.uxaml`)
  - `cortana_app.c` — the app (orb painter, SIP query flow, state machine)
  - `cortana_vec.h` — generated orb VEC data
  - `tests/` — e2e script + shot checker

## Build + e2e (host, NTOS_SH4_linux)

```
make -C edge/ntos_linux          # host env (needed by the e2e)
make -C cortana/app all test     # cortana.dll + fingerkb.dll + 6-frame e2e
```

The e2e walks home -> tap search -> type "weather" on the shared SIP ->
Thinking... -> offline error -> back home, checking the page palette,
the orb's blue ring, the greeting colour, the search band + mic square
and the keyboard backdrop per frame.

CI: the files-ci workflow builds + tests this app next to File Explorer
and ships the screenshots to the `files-ci` branch.
