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
static void navigation_paint(void *dc, const Render *r)
{
    r->clip(0, 488, 320, 528);
    r->fill(dc, 0, 488, 320, 528, PANEL);
    r->line(dc, 55, 508, 75, 508, INK);
    r->line(dc, 55, 508, 63, 500, INK);
    r->line(dc, 55, 508, 63, 516, INK);
    r->fill(dc, 151, 499, 159, 507, INK);
    r->fill(dc, 161, 499, 169, 507, INK);
    r->fill(dc, 151, 509, 159, 517, INK);
    r->fill(dc, 161, 509, 169, 517, INK);
    r->ellipse(dc, 258, 504, 7, 7, INK, 1);
    r->line(dc, 253, 510, 246, 518, INK);
    r->clip(0, 0, 320, 528);
}

static int nav = 1, panel, tracking, origin, press_x, press_y, pending = -1;
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
            CALL(svf_SVFDrawTint, (dc, icon->data, icon->length, x + 5, y + 4, 18, 18, label_color));
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
    if (!panel && nav && msg == WM_DOWN_ && y >= 488) {
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
    return nav ? 40 : 0;
}
static const Chrome chrome = {PROFILE,  paint,     input,
                              expanded, navheight, SetIsNavigationBarVisible};
const Chrome *UIXGetSchemaFactory(void)
{
    load_nav();
    return &chrome;
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
    return nav ? 40 : 0;
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
#ifndef PHONE_HOST
int main(void)
{
    return 1;
}
#endif
