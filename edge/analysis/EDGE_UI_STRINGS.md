EDGE 1607 (38.14393) MOBILE UI — exact en-US strings + control map
====================================================================
Source: resources.pri display-string blob @0x32c208 (en-US table,
analysis/pri_display_strings.txt) + XAML metadata heap (dataitem[0]).
This is ground truth for the uXAML markup rebuild.

Main screen (SmallScreenPage, dark theme)
-----------------------------------------
Bottom bar = address bar, full width, this order (user reference):
  [tabs icon] | [reload] [lock+two-tone URL | "Search or browse"] [reader(disabled)] | [...]
- placeholder: "Search or browse"
- navigated: lock + "en.wikipedia.org" where "wikipedia.org" is
  AddressBarHighlightedTextBrush (white) and "en." is
  AddressBarNonHighlightedTextBrush (greyed) — brushes named in pri heap.
- reader mode icon disabled on non-readable pages (IC_READER dimmed).

"..." ActionsMenu (bottom sheet)
--------------------------------
Options list (exact casing from pri):
  "Go forward"          (disabled unless forward stack non-empty)
  "Share"
  "Add to favorites"
  "Add to reading list"
  "Find on page"
  "Pin this page to Start"      <- capital S
Bottom row, 4 icons -> HubPanel pivots:
  Favorites (star) / Reading list (book) / History (clock) / Downloads (arrow)
Real menu also has: "New tab", "New InPrivate tab" (top buttons),
"Settings" (list, near end). We include Settings to reach the
clear-browsing-data screen.

TabCenterPage (tabs list)
-------------------------
- Title "Tabs" on top (automation "TABS").
- Grid of two columns per tab (TabsCustomGridView, TabsGridViewStyle).
- Card: title, url, favicon plate (TabsFaviconPlateBrush), current
  tab highlighted (TabsSelectedBackgroundBrush), close X.
- Bottom toolbar icons right-padded: "+" (TabsCenter_NewTabButton),
  slashed tab InPrivate (TabsCenter_NewInPrivateTabButton), "..."
  (m_tabsMenuFlyout).
- Tabs context menu (m_tabsMenuFlyout) options, exact:
  "Refresh all", "Duplicate tab", "Close tab", "Close other tabs",
  "Close tabs to the right", "Close all tabs", "Reopen closed tabs",
  "Pin tab"/"Unpin tab", "Move to new window", "Close all InPrivate tabs"

AddToReadingListPage (dialog)
-----------------------------
m_addToReadingList* controls; AddReadingListFlyout{Cancel,Commit}_Button,
AddReadingListFlyoutName. UI: label "Name" + text field with the page
title, buttons "Add" (commit) and "Cancel". Title "Add to reading list".

AddToFavoritesPage: same shape, plus "Folder" / "Create new folder".

Clear browsing data (from Settings)
-----------------------------------
Title "Clear browsing data". Checkboxes (Settings_Clear* + pri strings):
  "Browsing history"            (ClearHistory)
  "Cookies and saved website data" (ClearCookies)
  "Cached data and files"       (ClearTempFiles)
  "Download history"            (ClearDownloads)
  "Form data"                   (ClearForms)
  "Passwords"                   (ClearPasswords)
Button "Clear". Below, link texts:
  "Change what Microsoft Edge knows about me in the cloud"
  "Clear Bing search history"

Settings (MobileSettings) — real entries (subset we show)
---------------------------------------------------------
"Sync your favorites and reading list" / "Open new tabs with" /
"Search in the address bar with" / "Advanced settings" /
"Choose what to clear" (button -> Clear browsing data) /
"Send feedback" / "Help & about" / theme "Dark".

HubPanel pivots
---------------
Favorites / Reading list / History / Downloads.
History groups: "Today", "Last month". Downloads rows with progress
("%1 of %2"), "Pause"/"Resume"/"Cancel"/"Clear all".

AddressEditBox overlay
----------------------
Dark full-screen: input with placeholder "Search or browse",
suggestions (clock = visited, magnifier = search), SIP at bottom.
Enter/Go navigates. On NTP there is also a "New tab page" concept.

Misc exact strings used: "Refresh", "Back", "Close", "Cancel", "Add",
"Save", "Delete", "Clear", "Clear all", "Exit", "New tab",
"New InPrivate tab", "InPrivate" badge label, "Dark", "Settings".
