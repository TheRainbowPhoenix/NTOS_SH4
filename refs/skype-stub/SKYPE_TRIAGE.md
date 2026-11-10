# Microsoft.SkypeApp 2.29.0.10 — triage: STUB, not the full Skype

**Verdict: it is the WP8.1 "Welcome to Skype" store-upgrade STUB.**
The real Skype was shipped later as a Store update (much bigger package,
`Microsoft.SkypeApp_...` full UWP); this inbox package only exists to occupy
the app slot and funnel the user to the Store.

## Evidence

| check | value |
| --- | --- |
| main assembly | `SkypeStubApp.dll` (140 KB managed, MSIL) |
| host | `AGHost.exe` (WP8.1 Silverlight app host — not a UWP XAML app at all) |
| manifest | `WMAppManifest.xml` AppPlatformVersion 8.1, `AppManifest.xaml` RuntimeVersion 6.7.50308.0 (Silverlight) |
| pages | exactly ONE: `MainPage.xaml` (embedded text XAML, 70 KB — 90% is the logo storyboard) |
| package size | 566 KB zip / 1.3 MB unpacked (full Skype ≈ 30–60 MB) |
| languages | 56 satellite `SkypeStubApp.resources.dll`s, nothing but 4 strings + flow direction each |

## What the stub UI actually shows (MainPage.xaml, decoded here)

- full-bleed **Skype blue** `#FF00AFF0`, portrait, visible system tray
  (white on blue)
- `LogoAnimationStoryboard`: a 13-circle build-up animation that ends on the
  **Skype logo** — an "S" bubble (200px + two 100px outer circles) plus the
  vector **"Skype" wordmark** (Path `SkypeText`, clipped by a circle geometry)
  and a `SkypeTM` trademark mark, all white, inside a 192px Viewbox
- `WelcomeTitleTextBlock` — **"Welcome to Skype"**
- `WelcomeInfoTextBlock` — **"Call, chat and share with friends and family every day."**
- `GetItNowButton` — **"get it now"** (dark-blue `#FF0C7DAF` button,
  pressed `#FF1E739A`) → launches the Store listing
  (`Welcome_GetItNowFromStore_AppId` resource)
- `LearnMoreLink` hyperlink button — **"Learn more about Skype"**
  (`Welcome_LearnMoreAboutSkype_LINK`)

(en-GB/neutral strings decoded from the satellite resource set: the values
above; `ResourceFlowDirection=LeftToRight`.)

## Port value for NTOS

Low-but-nonzero: it is a *tiny*, fully-decoded Silverlight page. If we ever
want a "Skype" tile app for the W10M look, this stub is a 1-screen port
(logo animation + one button). It is NOT a source for the real Skype UI —
that would need the Store-era full package.

Files kept here: `mainpage.xaml`, `app.xaml`, `styles.xaml` (extracted
verbatim from `SkypeStubApp.dll` embedded resources), this triage note.
