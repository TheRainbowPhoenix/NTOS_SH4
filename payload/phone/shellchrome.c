#include "phone.h"
IMP(const Render *, render_SpGetThreadContext, (void))
IMP(int, svf_SVFDrawTint, (void *, const void *, unsigned, int, int, int, int, U32))
IMP(int, SipShowIM, (U32))
IMP(int, SipGetInfo, (SipInfo *))
IMP(void *, CreateFileW, (const W16 *, U32, U32, void *, U32, U32, void *))
IMP(int, ReadFile, (void *, void *, U32, U32 *, void *))
IMP(int, WriteFile, (void *, const void *, U32, U32 *, void *))
IMP(int, CloseHandle, (void *))
#include "system_icons.h"
static int nav_loaded, nav_touch, nav_key, nav_x, nav_y, closing;
static const W16 nav_file[] = {'\\', 'f', 'l', 's', '0', '\\', 'n',
                               'a',  'v', '.', 'd', 'a', 't',  0};
static void load_nav(void);
int SetIsNavigationBarVisible(int);
/* Status-bar colours can follow the hosting app's theme. */
static U32 nav_th(const Render *r, int slot, U32 fallback)
{
    return (r && r->theme) ? r->theme[slot] : fallback;
}
static void navigation_paint(void *dc, const Render *r)
{
    /* Navigation belongs to the system, so every app uses Start's palette. */
    U32 bg = PANEL;
    U32 ink = INK;
    /* the system nav bar owns the full 48px strip y480..528 (light
     * refs: toolbar e7e7e7 ends at y479, bar cecfce starts at y480);
     * painting the whole strip also erases any stale SIP pixels left
     * in the gap when the keyboard hid (the old 488..528 bar left an
     * 8px keyboard sliver visible under the toolbar). */
    r->clip(0, 480, 320, 528);
    r->fill(dc, 0, 480, 320, 528, bg);
    /* back: left-pointing arrow, 2px shaft + 3px chevron, centred on
     * the strip (settings_1.png: tip x54, shaft y503-504, arms to y495/512) */
    r->fill(dc, 55, 503, 75, 505, ink);
    for (int i = 0; i < 8; ++i) {
        r->fill(dc, 62 - i, 495 + i, 65 - i, 496 + i, ink);
        r->fill(dc, 55 + i, 505 + i, 58 + i, 506 + i, ink);
    }
    /* home: the W10 window flag — right pane taller than the left,
     * 1px centre split, diagonal corner cuts (settings_1.png) */
    r->fill(dc, 159, 495, 169, 512, ink); /* right pane  */
    r->fill(dc, 164, 494, 169, 495, ink); /* top corner  */
    r->fill(dc, 166, 512, 169, 513, ink); /* bottom edge */
    r->fill(dc, 150, 496, 158, 511, ink); /* left pane   */
    r->fill(dc, 150, 501, 158, 502, bg);  /* centre split */
    r->fill(dc, 159, 501, 169, 502, bg);
    /* search: magnifier — 2px ring + diagonal handle */
    r->ellipse(dc, 259, 500, 6, 6, ink, 2);
    r->line(dc, 252, 507, 246, 513, ink);
    r->line(dc, 253, 507, 247, 513, ink);
    r->clip(0, 0, 320, 528);
}

/* ---- hosted-app status bar: the system title bar, launcher-owned ----
 * Hosted apps never draw their own top bar: the shared chrome paints it
 * over their surface (y0..24), themed through the hosting app's table
 * like the nav bar — white bg + near-black ink on light hosts, black bg
 * + white ink on dark ones (derived from the published NAV_BG).
 * Geometry from the W10M device captures (refs/calc-screens): five
 * ascending signal bars bottom-aligned at y17 (x13..26), and at the
 * right a battery (box x246..268 y7..16, nub on the top edge) with the
 * clock right-aligned to x306. */
static void status_paint(void *dc, const Render *r)
{
    U32 navbg = nav_th(r, UIX_TH_NAV_BG, PANEL);
    int lum = (int)((navbg >> 16 & 0xff) * 30 + (navbg >> 8 & 0xff) * 59 +
                    (navbg & 0xff) * 11);
    int light = lum >= 12800; /* 128 * 100 */
    U32 bg = light ? WHITE : RGB_(0x1f, 0x1f, 0x1f);
    U32 ink = light ? RGB_(0x18, 0x18, 0x21) : WHITE;
    r->clip(0, 0, 320, 24);
    r->fill(dc, 0, 0, 320, 24, bg);
    /* signal: five 2px bars, heights 3,5,7,9,11, bottoms at y17 */
    for (int i = 0; i < 5; ++i)
        r->fill(dc, 13 + 3 * i, 15 - 2 * i, 15 + 3 * i, 17, ink);
    /* battery: 1px box + charged interior + nub on the top edge */
    r->line(dc, 246, 7, 268, 7, ink);
    r->line(dc, 246, 16, 268, 16, ink);
    r->line(dc, 246, 7, 246, 16, ink);
    r->line(dc, 268, 7, 268, 16, ink);
    r->fill(dc, 248, 9, 266, 14, ink);
    r->fill(dc, 250, 5, 252, 7, ink);
    /* clock, right-aligned to x306 (12px glyphs y8..16) */
    r->text(dc, 271, 8, "12:50", ink, 12);
    r->clip(0, 0, 320, 528);
}

static int nav = 1, panel, tracking, origin, press_x, press_y, pending = -1;
static int hosted_status_visible = 1;
static unsigned toggles = (1u << 5) | (1u << 13);
static char notification[64];
static const char *labels[16] = {
    "Connect",  "Battery saver", "Flashlight",    "Note",           "VPN",  "Location",
    "Airplane", "Camera",        "Cellular data", "Mobile hotspot", "Exit", "Soft keys",
    "Wi-Fi",    "Not connected", "Rotation lock", "All settings"};
static const unsigned icons[16] = {SYS_CONNECT, SYS_BATTERY,   SYS_FLASH,    SYS_NOTE,
                                   SYS_VPN,     SYS_LOCATION,  SYS_AIRPLANE, SYS_CAMERA,
                                   SYS_CELL,    SYS_HOTSPOT,   SYS_PREV,     SYS_SUN,
                                   SYS_WIFI,    SYS_BLUETOOTH, SYS_LOCK,     SYS_SETTINGS};
static void bar(void *dc, const Render *r)
{
    r->fill(dc, 0, 0, 320, 24, PANEL);
    for (int i = 0; i < 5; ++i)
        r->fill(dc, 13 + 3 * i, 16 - 2 * i, 15 + 3 * i, 18, GRAY);
    r->line(dc, 249, 7, 266, 7, GRAY);
    r->line(dc, 249, 16, 266, 16, GRAY);
    r->line(dc, 249, 7, 249, 16, GRAY);
    r->line(dc, 266, 7, 266, 16, GRAY);
    r->fill(dc, 251, 9, 263, 15, GRAY);
    r->fill(dc, 267, 10, 269, 14, GRAY);
    r->text(dc, 283, 7, "8:02", GRAY, 12);
}
static void paint(void *dc)
{
    const Render *r = CALL(render_SpGetThreadContext, ());
    r->clip(0, 0, 320, 528);
    if (panel) {
        r->fill(dc, 0, 0, 320, 528, SURFACE);
        r->text(dc, 10, 30, "Demo GSM Network LTE+", GRAY, 9);
        r->text(dc, 244, 30, "100%   11/9", GRAY, 9);
        for (unsigned i = 0; i < 16; ++i) {
            int x = 4 + (int)(i % 4) * 79, y = 65 + (int)(i / 4) * 64;
            U32 c = toggles & (1u << i) ? BLUE : CARD;
            U32 label_color = toggles & (1u << i) ? WHITE : INK;
            r->fill(dc, x, y, x + 75, y + 59, c);
            const SysIcon *icon = &system_icons[icons[i]];
            CALL(svf_SVFDrawTint,
                 (dc, icon->data, icon->length, x + 5, y + 4, 18, 18, label_color));
            if (i == 6) {
                r->text(dc, x + 5, y + 28, "Airplane", label_color, 12);
                r->text(dc, x + 5, y + 43, "mode", label_color, 12);
            } else if (i == 9) {
                r->text(dc, x + 5, y + 28, "Mobile", label_color, 12);
                r->text(dc, x + 5, y + 43, "hotspot", label_color, 12);
            } else if (i == 13) {
                r->text(dc, x + 5, y + 28, "Not", label_color, 12);
                r->text(dc, x + 5, y + 43, "connected", label_color, 12);
            } else
                r->text(dc, x + 5, y + 40, labels[i], label_color,
                        i == 1 || i == 8 || i == 14 || i == 15 ? 11 : 12);
        }
        r->text(dc, 262, 337, "Collapse", GRAY, 12);
        if (notification[0]) {
            r->text(dc, 12, 382, "Shell", INK, 14);
            r->text(dc, 12, 407, notification, GRAY, 12);
            r->text(dc, 255, 452, "Clear all", GRAY, 12);
        } else
            r->text(dc, 12, 382, "No notifications", GRAY, 16);
        r->fill(dc, 0, 512, 320, 528, BLUE);
        r->fill(dc, 152, 518, 168, 519, WHITE);
        r->fill(dc, 152, 521, 168, 522, WHITE);
    }
    bar(dc, r);
    if (nav && !panel)
        navigation_paint(dc, r);
}
static int input(U32 msg, U32 wp, U32 lp)
{
    int x = (int)(short)(lp & 65535u), y = (int)(short)(lp >> 16);
    if (closing) {
        if (msg == WM_UP_)
            closing = tracking = 0;
        return 1;
    }
    if (nav_touch) {
        if (msg == WM_MOVE_ &&
            (x - nav_x > 10 || nav_x - x > 10 || y - nav_y > 10 || nav_y - y > 10))
            nav_key = -1;
        if (msg == WM_UP_) {
            int key = nav_key;
            if (x - nav_x > 10 || nav_x - x > 10 || y - nav_y > 10 || nav_y - y > 10)
                key = -1;
            nav_touch = 0;
            return key < 0 ? 1 : 4 + key;
        }
        return 1;
    }
    if (!panel && nav && msg == WM_DOWN_ && y >= 480) {
        nav_touch = 1;
        nav_x = x;
        nav_y = y;
        nav_key = x < 106 ? 0 : x < 213 ? 1 : 2;
        return 1;
    }
    if (msg == WM_KEY_ && panel && (wp == 0x1b || wp == 0x25)) {
        panel = tracking = 0;
        pending = -1;
        return 1;
    }
    if ((msg == WM_MOVE_ || (msg == WM_DOWN_ && tracking)) && tracking) {
        if (!panel && y - origin > 25) {
            panel = 1;
            origin = y;
            pending = -1;
            CALL(SipShowIM, (0));
            return 1;
        }
        if (panel) {
            if (x - press_x > 8 || press_x - x > 8 || y - press_y > 8 || press_y - y > 8)
                pending = -1;
            if (y > origin)
                origin = y;
            if (origin - y > 32) {
                panel = 0;
                closing = 1;
                pending = -1;
                return 1;
            }
            if (y != origin)
                pending = -1;
            return 1;
        }
        return 1;
    }
    if (msg == WM_DOWN_) {
        tracking = panel || y < 24;
        origin = press_y = y;
        press_x = x;
        pending = -1;
        if (!tracking)
            return 0;
        if (panel && y >= 65 && y < 321 && x >= 4 && x < 316) {
            int col = div_(x - 4, 79), row = div_(y - 65, 64);
            if (col < 4 && row < 4)
                pending = row * 4 + col;
        } else if (panel && y >= 330 && y < 365)
            pending = 16;
        else if (panel && y >= 505)
            pending = 16;
        else if (panel && y >= 440 && y < 490)
            pending = 17;
        return 1;
    }
    if (msg == WM_UP_) {
        if (panel && tracking && press_y - y > 24) {
            panel = tracking = 0;
            pending = -1;
            return 1;
        }
        int was = tracking;
        int item = pending;
        pending = -1;
        tracking = 0;
        if (!panel)
            return was;
        if (item == 16) {
            panel = 0;
            return 1;
        }
        if (item == 17) {
            notification[0] = 0;
            return 1;
        }
        if (item == 11) {
            SetIsNavigationBarVisible(!nav);
            return 1;
        }
        if (item == 15) {
            panel = 0;
            return 2;
        }
        if (item == 10) {
            panel = 0;
            return 3;
        }
        if (item >= 0 && item < 16) {
            toggles ^= 1u << item;
            const char *s = labels[item];
            unsigned n = 0;
            while (s[n] && n < 45) {
                notification[n] = s[n];
                ++n;
            }
            notification[n] = 0;
        }
        return 1;
    }
    return panel;
}
static int expanded(void)
{
    return panel;
}
static int navheight(void)
{
    return nav ? 48 : 0;
}
static const Chrome chrome = {PROFILE,  paint,     input,
                              expanded, navheight, SetIsNavigationBarVisible};
const Chrome *UIXGetSchemaFactory(void)
{
    load_nav();
    return &chrome;
}
/* ---- hosted-app chrome: status bar + nav bar ----
 * Edge and other hosted apps import UIXChromeFactory instead of the full
 * shell chrome: status, navigation, and the shared notification panel.
 * The status and navigation bars follow the hosting renderer theme. */
static void hosted_paint(void *dc)
{
    const Render *r = CALL(render_SpGetThreadContext, ());
    if (panel) {
        paint(dc);
        return;
    }
    if (hosted_status_visible)
        status_paint(dc, r);
    if (nav)
        navigation_paint(dc, r);
}
static int hosted_input(U32 msg, U32 wp, U32 lp)
{
    return input(msg, wp, lp);
}
static int hosted_expanded(void)
{
    return panel;
}
static const Chrome hosted_chrome = {PROFILE, hosted_paint, hosted_input,
                                    hosted_expanded, navheight,
                                    SetIsNavigationBarVisible};
#ifdef NTOS_LINUX_BUILD
__attribute__((dllexport))
#endif
const Chrome *UIXChromeFactory(void)
{
    load_nav();
    hosted_status_visible = 1;
    return &hosted_chrome;
}
#ifdef NTOS_LINUX_BUILD
__attribute__((dllexport))
#endif
void UIXSetStatusBarVisible(int visible)
{
    hosted_status_visible = visible != 0;
}
void UIXSchemaUnloaded(void)
{
    panel = tracking = 0;
}
int InitializeShellChromeComponents(void)
{
    panel = tracking = closing = nav_touch = 0;
    load_nav();
    return 0;
}
int GetSipVisibility(void)
{
    SipInfo s;
    s.size = sizeof(s);
    return CALL(SipGetInfo, (&s)) ? (int)(s.flags & 1u) : 0;
}
int SetSipVisibility(int visible)
{
    return CALL(SipShowIM, (visible ? 1u : 0u)) ? 0 : -1;
}
int GetNavigationBarHeight(void)
{
    return nav ? 48 : 0;
}
int GetIsNavigationBarVisible(void)
{
    return nav;
}
static void load_nav(void)
{
    if (nav_loaded)
        return;
    nav_loaded = 1;
    void *f = CALL(CreateFileW, (nav_file, 0x80000000u, 1, 0, 3, 0, 0));
    if (f == (void *)(intptr_t)-1)
        return;
    unsigned char value = 1;
    U32 got = 0;
    if (CALL(ReadFile, (f, &value, 1, &got, 0)) && got == 1 && value <= 1)
        nav = value;
    CALL(CloseHandle, (f));
}
int SetIsNavigationBarVisible(int visible)
{
    nav = visible != 0;
    nav_loaded = 1;
    unsigned char value = (unsigned char)nav;
    U32 done = 0;
    void *f = CALL(CreateFileW, (nav_file, 0x40000000u, 1, 0, 2, 0, 0));
    if (f != (void *)(intptr_t)-1) {
        CALL(WriteFile, (f, &value, 1, &done, 0));
        CALL(CloseHandle, (f));
    }
    return 0;
}
int GetLockScreenVisible(void)
{
    return 0;
}
int GetWaitCursorVisible(void)
{
    return 0;
}
int phone_unsupported(void)
{
    return NOTIMPL;
}
#ifdef NTOS_LINUX_BUILD
/* Host PE DLL: minimal CRT entry + suppress the ELF-style main() so the
 * freestanding link works (same pattern as fingerkb.dll). */
int DllMainCRTStartup_impl(void *h, U32 r, void *r2)
{
    (void)h;
    (void)r;
    (void)r2;
    return 1;
}
__asm__(".globl _DllMainCRTStartup\n_DllMainCRTStartup = DllMainCRTStartup_impl\n");
__asm__(".globl __main\n__main = DllMainCRTStartup_impl\n");
#else
#ifndef PHONE_HOST
int main(void)
{
    return 1;
}
#endif
#endif
