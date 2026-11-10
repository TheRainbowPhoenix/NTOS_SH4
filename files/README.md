# NTOS files — File Explorer (W10M) port

Port of `Microsoft.Windows.FileExplorer_cw5n1h2txyewy` (Windows 10 Mobile
RS1 inbox app) to NTOS with the tiny uXAML engine, following the edge/ POC
model (shared markup engine, shared FINGERKB.DLL keyboard, shared uixctl
control painters — but the W10M **light** theme).

## Layout

- `analysis/` — everything extracted from the reference package
  - `FILEEXPLORER_UI_REFERENCE.md` — the port spec (pages, type ramp,
    palette, geometry, strings)
  - `pri_en_us.txt` — all 234 en-US display strings (ground truth)
  - `pri_resources.txt`, `pri_out/` — full mrm_pri2 decode (65 languages)
  - `xbf_raw/` — the 17 compiled XAML pages carved out of resources.pri
  - `xbf_dump/` — every page decoded to a readable object tree
  - `FileExplorer_strings.txt`, `pe_scan/`, `strings/` — exe triage
- `tools/` — reusable analysis tooling
  - `pri_decode.py` (full mrm_pri2 decoder), `pri_dump_all.py`,
    `pri_extract_files.py`, `xbf_dump.py` + `xbf_framework_types.py`
    (XBF v2 decoder), `ui_strings.py`, `vec_icons.py` (multi-layer
    colour VEC2 type icons -> `app/files_vecicons.h`)
- `app/` — the POC
  - `markup/*.xml` — the uXAML pages (compiled by the shared
    `edge/app/uxaml_gen.py` into `files.uxaml`)
  - `files_app.c` — app logic + real \fls0\ file operations
  - `files_icons.c/h` — MDL2-style monochrome UI glyphs
  - `files_vecicons.h` — generated multi-layer colour VEC type icons
  - `tests/` — e2e script, sample tree builder, shot checker, fs verifier

## Build + e2e (host, NTOS_SH4_linux)

```
make -C edge/ntos_linux          # host env (needed by the e2e)
make -C files/app all test       # files.dll + fingerkb.dll + 27-frame e2e
```

`make test` builds the sample \fls0\ tree, runs the scripted UI session
under `edge/ntos_linux/ntos_linux` (browse, nav pane, select mode, sort
flyout, overflow menu, properties, new-folder + rename typed on the
shared SIP, recursive search, delete, copy AND move through the
destination picker, drag-scrolling, and the file:// open-in-Edge hand-off)
and pixel-checks every frame; `tests/verify_fs.py` then asserts the
rename/move/copy physically landed on disk.

CI: `.forgejo/workflows/files-ci.yml` ships the log + screenshots to the
`files-ci` branch on every push.
