# SystemSettings (W10M) port

Settings app (Microsoft.Windows.SystemSettings_cw5n1h2txyewy, RS1
10.0.14393) reconstructed for NTOS from the package XBFs + en-US
resources and calibrated against the real-device captures in
`../refs/settings-screens/` (light theme).

- `analysis/` — pri/XBF dumps + `SYSTEMSETTINGS_UI_REFERENCE.md`
  (the port spec: layout, palette, control geometry).
- `app/` — the POC: uXAML markup + settings_app.c +
  settings_icons.c, built with the shared edge/files/cortana
  components (uxaml.c engine, uixctl/svf controls, FINGERKB.DLL
  keyboard, SHELLCHROME.DLL navbar).
  `make test` runs the scripted e2e under NTOS_SH4_linux and
  verifies 26 screenshots with pixel probes.
- `tools/` — glyph-context scanners used during the analysis.
- CI: `.forgejo/workflows/settings-ci.yml` (log + shots on the
  `settings-ci` branch).
