#!/usr/bin/env python3
"""ui_map.py — consolidate all pri/string analysis into the Edge UI blueprint.

Groups the 1939 automation keys (edge/analysis/pri_hschema_strings.txt) by
UI region (page/control), merges the 41 XBF page names and the ViewModel
class list. Output: edge/analysis/EDGE_UI_MAP.txt — the single reference the
NTOS Edge POC implementation follows (blind reconstruction ground truth).
"""
import os, re, sys

HERE = os.path.dirname(__file__)
A = os.path.join(HERE, '..', 'analysis')

# UI regions: automation-key prefix -> (region, page, description)
REGIONS = [
    (r'^SmallScreen', 'SmallScreenPage', 'phone main page chrome'),
    (r'^LargeScreen', 'LargeScreenPage', 'tablet main page chrome'),
    (r'^TravelBar', 'SmallScreenPage/TravelBar', 'back/forward/refresh navigation bar'),
    (r'^AddressEditBox', 'SmallScreenPage/AddressEditBox', 'URL box + badges'),
    (r'^OneBox', 'OneBoxLite', 'lite suggestion box'),
    (r'^Tab[sS]', 'Tabs/TabCenterPage', 'tab strip, thumbnails, tab switcher'),
    (r'^TabsContextMenu', 'Tabs/TabCenterPage context menu', 'per-tab context menu'),
    (r'^TabPeek', 'TabThumbnailItem/peek', 'tab preview overlay'),
    (r'^ActionsMenu', 'ActionsMenu sheet', 'the "..." overflow menu'),
    (r'^Hub', 'HubPanel', 'favorites/history/reading list/downloads hub'),
    (r'^HubFavorite', 'HubPanel/FavoritesPane', 'favorites pivot'),
    (r'^HubHistory', 'HubPanel/HistoryPane', 'history pivot'),
    (r'^HubReadingList', 'HubPanel/ReadingListPane', 'reading list pivot'),
    (r'^Download', 'HubPanel/DownloadsPane', 'downloads pivot'),
    (r'^AddFavorite', 'AddToFavoritesPage', 'add favorite flyout'),
    (r'^AddToReadingList', 'AddToReadingListPage', 'add reading list flyout'),
    (r'^SaveFavorite', 'SaveFavoritesForm', 'favorites import/save form'),
    (r'^SaveReading', 'SaveReadingListForm', 'reading list form'),
    (r'^ImportFavorite', 'ImportFavoritesPanel', 'import favorites panel'),
    (r'^FindOnPage', 'FindOnPageBar', 'find-on-page bar'),
    (r'^Pdf', 'PdfBar/PdfBarSmallScreen', 'PDF viewer bar'),
    (r'(Annotation|PdfAction)', 'PdfBar annotation buttons', 'PDF ink toolbar'),
    (r'^Notification', 'NotificationBar/NotificationDialog', 'site notifications'),
    (r'PromptDialog', 'PromptDialog', 'JS alert/confirm/prompt dialog'),
    (r'InformationDialog', 'InformationDialog', 'info dialog'),
    (r'Password', 'PasswordDialog/VaultPasswordsPane', 'credential dialogs/vault'),
    (r'^Vault', 'VaultPasswordsPane', 'saved passwords pane'),
    (r'^Settings', 'Settings/MobileSettings', 'settings app pages'),
    (r'^Feedback', 'FeedbackPanel(Mobile)', 'smiley feedback panel'),
    (r'^DeleteHistory', 'DeleteHistoryPanel', 'clear browsing data panel'),
    (r'WebsitePermission', 'WebsitePermissionsPane', 'per-site permissions'),
    (r'Credential', 'VaultPasswordsPane', 'credentials UI'),
    (r'WebPermissions', 'WebsitePermissionsPane', 'permissions list'),
    (r'^Badge', 'Left/RightBadgeBox', 'address bar badges'),
    (r'^Cortana', 'CortanaAssist', 'cortana pane'),
    (r'^TrustBadge', 'AddressEditBox badges', 'cert/smartscreen badge'),
    (r'^Share|SendPage|ReadingView|^Read', 'ActionsMenu actions', 'page-level actions'),
    (r'^NewTab', 'NewTabPage', 'new tab page'),
    (r'^Privacy|^InPrivate', 'Tabs/InPrivate', 'InPrivate indicators'),
    (r'^Exit|^Close|^More|^Menu', 'SmallScreenPage buttons', 'chrome buttons'),
]


def load_keys():
    keys = []
    for line in open(os.path.join(A, 'pri_hschema_strings.txt')):
        m = re.match(r'^0x[0-9a-f]+ (.+)$', line.strip())
        if m:
            s = m.group(1)
            if re.match(r'^[A-Za-z][A-Za-z0-9_]*$', s) and len(s) > 3:
                keys.append(s)
    return sorted(set(keys))


def load_xbf_pages():
    pages = set()
    for src in ('pri_xaml_heap_strings.txt', 'pri_hschema_strings.txt'):
        path = os.path.join(A, src)
        if not os.path.exists(path):
            continue
        for line in open(path):
            m = re.match(r'^0x[0-9a-f]+ ([A-Za-z0-9_]+\.xbf)$', line.strip())
            if m:
                pages.add(m.group(1))
    return sorted(pages)


def load_viewmodels():
    vms = set()
    path = os.path.join(A, 'strings', 'MicrosoftEdge.exe.winrt.txt')
    for line in open(path):
        m = re.match(r'^class-u16\s+\d+\s+(SpModel\.[A-Za-z0-9_.]+)$', line.strip())
        if m:
            vms.add(m.group(1))
    return sorted(vms)


def load_styles():
    out = {'style': set(), 'brush': set(), 'state': set(), 'ctrl': set()}
    for line in open(os.path.join(A, 'pri_xaml_heap_strings.txt')):
        m = re.match(r'^0x[0-9a-f]+ (.+)$', line.strip())
        if not m:
            continue
        s = m.group(1)
        if re.match(r'^[a-z]+.*Style$', s):
            out['style'].add(s)
        if s.endswith('Brush') or s.endswith('ThemeBrush') or s.endswith('Color'):
            out['brush'].add(s)
        if re.match(r'^(CommonStates|FocusStates|SelectionStates|Pressed|PointerOver|Normal|Disabled|Checked|DragStates)$', s):
            out['state'].add(s)
        if s.startswith('m_'):
            out['ctrl'].add(s)
    return {k: sorted(v) for k, v in out.items()}


def main():
    keys = load_keys()
    pages = load_xbf_pages()
    vms = load_viewmodels()
    styles = load_styles()
    lines = []
    w = lines.append
    w('EDGE MOBILE UI MAP (blind reconstruction ground truth)')
    w('source: Microsoft.MicrosoftEdge_38.14393.0.0 (Windows.Mobile UWP, RS1)')
    w('=' * 78)
    w('')
    w(f'{len(pages)} XAML pages (resources.pri Files/ tree):')
    for p in pages:
        w(f'  {p}')
    w('')
    w(f'{len(vms)} SpModel ViewModel/runtime classes:')
    for v in vms:
        w(f'  {v}')
    w('')
    w(f'{len(styles["style"])} styles, {len(styles["brush"])} brushes, '
      f'{len(styles["ctrl"])} named controls (m_*)')
    w('')
    w('Automation keys by UI region:')
    w('-' * 78)
    grouped = {r: [] for r in REGIONS}
    rest = []
    for k in keys:
        for pat, region, desc in REGIONS:
            if re.search(pat, k):
                grouped[(pat, region, desc)].append(k)
                break
        else:
            rest.append(k)
    for (pat, region, desc), ks in grouped.items():
        if not ks:
            continue
        w(f'')
        w(f'## {region} — {desc}  [{len(ks)} keys]')
        for k in ks:
            w(f'   {k}')
    if rest:
        w('')
        w(f'## (unclassified) [{len(rest)}]')
        for k in rest:
            w(f'   {k}')
    out = os.path.join(A, 'EDGE_UI_MAP.txt')
    open(out, 'w').write('\n'.join(lines) + '\n')
    print(f'wrote {out}: {len(pages)} pages, {len(vms)} viewmodels, {len(keys)} keys')


if __name__ == '__main__':
    main()
