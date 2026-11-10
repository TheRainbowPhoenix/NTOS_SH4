# Cortana UI reference (Microsoft.Windows.Cortana_cw5n1h2txyewy, RS1)

Port spec distilled from the package's `Views/*.xbf` + `resources.pri`
(cortana/analysis/pri_out/) and the real-device captures in `refs/cortana/`
(cortana_light.png, cortana_light_load.png, cortana_light_failed.png).
The app is the demo piece of the suite — no speech stack, no network:
the conversation UI itself is the deliverable.

## Package triage

- `SearchUI.exe` 7.1 MB — the UI process; 51 `Views/*.xbf` pages ship as
  loose files (unlike FileExplorer's pri-embedded XBFs): Chrome.xbf (the
  shell chrome), CortanaWindow.xbf, SearchBoxControl.xbf,
  GreetingsControl.xbf, ErrorPage.xbf, COOBEPage.xbf (first-run consent,
  the `ask.png` capture), MusicRecoPage.xbf ...
- `resources.pri` 3.4 MB -> 1253 resources, 1246 en-US values
  (files/tools/pri_decode.py, same decoder as FileExplorer).
- The service fabric (CortanaApi.dll 6.7 MB, Signals 2.5 MB, node.dll,
  292 Grammar files ...) is out of scope: NTOS has no network stack, so
  the port models exactly what the device shows offline.

## Ground-truth strings (pri_en_us.txt)

| Resource | Value |
| --- | --- |
| greetingsresources/Fallback_Text_8 | `How can I help?` |
| ProactiveControl_CortanaErrorTextBlock/Text | `Go ahead and tell me to do something. Interests aren't available at the moment.` |
| SpeechTextInput_Thinking | `Thinking...` |
| Generic_ErrorTextBlock/Text | `I can't connect at the moment.\n\nMake sure you are currently connected to the internet.` |
| MusicRecoPage_RecognitionStatusNetworkError | `I can't find songs right now. Make sure you're connected to the Internet and try again.` |

## Home layout (measured, cortana_light.png, 320x528)

| Element | Geometry |
| --- | --- |
| Page background | `#F7F3F7` (warm light gray, not the files app's #FFFFFF) |
| Hamburger / music icons | (16,16) d16 / (284,12) d24, ink #333333 / #5A5A5A |
| The orb | circle centre (160,48), outer d≈48: pale periwinkle halo ring + thick `#4A71FF` ring with a small gap at the lower right (the loading opening) |
| Greeting | "How can I help?" `#3E6BE0`, ~15px, centred, y84 |
| Body text | 20px, near-black, left at x12, 3 wrapped lines starting y138 (32px pitch) |
| Search band | y432..480 `#DEDBDE`, "Type here to search" placeholder |
| Mic button | ~44x44 `#0078D7` square at (268,434), white mic glyph |

## States

- **Home** (cortana_light.png): the layout above. The reference captures
  the "Interests aren't available" offline copy — exactly this device's
  situation, so it doubles as the honest state.
- **Input**: the shared FINGERKB SIP covers y271..528; the query field
  floats above it (the files-app search pattern). Enter submits.
- **Thinking** (cortana_light_load.png): the orb switches to the rotated
  larger-gap variant; a `#DEDBDE` strip shows `Thinking...` with an X
  cancel at the right.
- **Error** (cortana_light_failed.png): any query resolves to the
  Generic_ErrorTextBlock copy, centred black on the light page — no
  network on NTOS, same as the reference device in flight mode.
- **Music button** -> MusicReco network error (same offline answer).

## Implementation notes

- The orb is a layered VEC2 asset (cortana/tools/vec_cortana.py): white
  field, halo arc, ring arc — rendered through the shared svf.c engine at
  48px. NOTE: VEC arcs stroke with the SETSTYLE *stroke* colour; fills
  only apply to circles/rects/polys.
- Root-level centred markup text needs `x="0" w="320"` — the uXAML layout
  inherits the parent width when w is unset, so `x=160 align=center`
  lands at 320.
- Component reuse: uxaml.c (engine), files_icons.c (hamburger/mic/music/X
  glyphs — FI_MIC added), uixctl.c + svf.c (controls + VEC), FINGERKB.DLL
  (keyboard). The renderer table is the light keyboard theme, like files.
