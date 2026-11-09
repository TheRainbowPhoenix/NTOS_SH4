/* Prepared native Settings views; values are local previews, not hardware configuration. */
#include "phone.h"
IMP(const Render *, render_SpGetThreadContext, (void))
IMP(int, svf_SVFDraw, (void *, const void *, unsigned, int, int, int, int))
IMP(int, SipShowIM, (U32))
#include "settings_icons.h"
static int content_bottom = 528;
static void bounds(int b)
{
    content_bottom = b;
}
static int view, category, scroll_y, drag_y, drag_scroll, search, len, tracking, moved;
static const char *leaf = "Settings";
static int developer = 2, encryption;
static int brightness = 50, automatic = 1, rotation, scale = 100;
static int saved_brightness = 50, saved_automatic = 1, saved_rotation, saved_scale = 100;
static char query[40];
static const char *names[] = {"System",          "Devices",  "Network & wireless",
                              "Personalization", "Accounts", "Time & language",
                              "Ease of Access",  "Privacy",  "Update & security"};
static const char *details[] = {"Display, notifications, battery",
                                "Bluetooth, camera",
                                "Wi-Fi, airplane mode, cellular",
                                "Start, lock screen, sounds",
                                "Your accounts, email, sync",
                                "Speech, region, keyboard",
                                "Vision, hearing, interaction",
                                "Location, feedback",
                                "Backup, Find My Phone"};
static const char *system_names[] = {
    "Display",     "Notifications & actions", "Phone", "Messaging", "Storage", "Battery",
    "Offline maps"};
static const char *update_names[] = {"Phone update",  "Backup",         "Device encryption",
                                     "Find My Phone", "For developers", "Windows Insider Program"};
static void icon(void *dc, unsigned which, int x, int y, int size)
{
    const SettingsIcon *i = &settings_icons[which];
    CALL(svf_SVFDraw, (dc, i->data, i->length, x, y, size, size));
}
static int matches(const char *s)
{
    if (!len)
        return 1;
    for (unsigned j = 0; s[j]; ++j) {
        int i = 0;
        while (i < len && s[j + i]) {
            int a = s[j + i], b = query[i];
            if (a >= 'A' && a <= 'Z')
                a += 32;
            if (b >= 'A' && b <= 'Z')
                b += 32;
            if (a != b)
                break;
            ++i;
        }
        if (i == len)
            return 1;
    }
    return 0;
}
static void toggle(void *dc, const Render *r, int y, int on)
{
    U32 c = on ? BLUE : GRAY;
    r->fill(dc, 12, y, 56, y + 20, c);
    r->fill(dc, 14, y + 2, 54, y + 18, on ? BLUE : 0);
    r->fill(dc, on ? 40 : 16, y + 4, on ? 52 : 28, y + 16, WHITE);
    r->text(dc, 68, y + 2, on ? "On" : "Off", WHITE, 14);
}
static void slider(void *dc, const Render *r, int y, int value)
{
    int x = 12 + div_(value * 276, 100);
    r->fill(dc, 12, y, 292, y + 2, RGB_(100, 100, 100));
    r->fill(dc, 12, y, x, y + 2, BLUE);
    r->fill(dc, x - 4, y - 8, x + 4, y + 11, BLUE);
    r->ellipse(dc, x, y - 8, 4, 4, BLUE, 0);
    r->ellipse(dc, x, y + 10, 4, 4, BLUE, 0);
}
static void paint(void *dc)
{
    const Render *r = CALL(render_SpGetThreadContext, ());
    r->clip(0, 24, 320, 528);
    r->fill(dc, 0, 24, 320, 528, view >= 2 && view != 4 ? 0 : RGB_(23, 23, 23));
    r->fill(dc, 0, 24, 320, 72, RGB_(23, 23, 23));
    icon(dc, 8, 12, 36, 24);
    r->text(dc, 48, 40,
            view == 2   ? "Display"
            : view == 5 ? "For developers"
            : view == 6 ? "Device encryption"
            : view == 7 ? "Phone update"
            : view == 8 ? leaf
                        : "Settings",
            WHITE, 14);
    if (view == 0 || view == 1 || view == 4) {
        r->fill(dc, 12, 80, 308, 112, BLUE);
        r->fill(dc, 14, 82, 306, 110, WHITE);
        r->text(dc, 23, 89, len ? query : search ? "" : "Find a setting", search ? 0 : GRAY, 14);
        if (search)
            r->fill(dc, 23 + len * 7, 87, 24 + len * 7, 102, 0);
        r->ellipse(dc, 292, 93, 5, 5, GRAY, 1);
        r->line(dc, 296, 97, 300, 101, GRAY);
    }
    if (view == 0) {
        r->clip(0, 122, 320, 528);
        int row = 0;
        for (unsigned i = 0; i < 9; ++i) {
            if (!matches(names[i]))
                continue;
            int y = 132 + row++ * 64 - scroll_y;
            icon(dc, i < 8 ? i : 8, 12, y + 4, 32);
            r->text(dc, 54, y + 3, names[i], WHITE, 14);
            r->text(dc, 54, y + 24, details[i], GRAY, 12);
        }
    } else if (view == 1) {
        r->text(dc, 12, 143, "System", GRAY, 14);
        r->clip(0, 178, 320, 528);
        for (unsigned i = 0; i < 7; ++i) {
            int y = 186 + (int)i * 48 - scroll_y;
            icon(dc, 9 + i, 12, y + 2, 17);
            r->text(dc, 39, y + 4, system_names[i], WHITE, 14);
        }
    } else if (view == 2) {
        r->text(dc, 12, 98, "Display", WHITE, 22);
        r->text(dc, 12, 147, "Adjust brightness level", WHITE, 14);
        slider(dc, r, 181, brightness);
        r->text(dc, 12, 211, "Change brightness automatically when", WHITE, 14);
        r->text(dc, 12, 232, "lighting changes", WHITE, 14);
        toggle(dc, r, 257, automatic);
        r->text(dc, 12, 302, "Rotation lock", WHITE, 14);
        toggle(dc, r, 327, rotation);
        r->text(dc, 12, 371, "Size of text, apps, and items on this display", WHITE, 14);
        slider(dc, r, 413, scale);
        r->fill(dc, 12, 439, 102, 471, RGB_(46, 46, 46));
        r->text(dc, 38, 449, "Apply", WHITE, 14);
        r->fill(dc, 122, 439, 212, 471, RGB_(46, 46, 46));
        r->text(dc, 145, 449, "Cancel", WHITE, 14);
        r->text(dc, 12, 503, "Show a dot on the connected display when", WHITE, 14);
    } else if (view == 4) {
        r->text(dc, 12, 143, "Update & security", GRAY, 14);
        r->clip(0, 178, 320, 528);
        for (unsigned i = 0; i < 6; ++i) {
            int y = 186 + (int)i * 48 - scroll_y;
            icon(dc, i == 2 ? 7 : 8, 12, y + 2, 17);
            r->text(dc, 39, y + 4, update_names[i], WHITE, 14);
        }
    } else if (view == 5) {
        r->text(dc, 12, 101, "Use developer features", WHITE, 22);
        r->text(dc, 12, 148, "These settings are intended for", WHITE, 14);
        r->text(dc, 12, 169, "development use only.", WHITE, 14);
        r->text(dc, 12, 199, "Learn more", BLUE, 14);
        static const char *choices[] = {"Windows Store apps", "Sideload apps", "Developer mode"};
        for (int i = 0; i < 3; ++i) {
            int y = 238 + i * 90;
            r->fill(dc, 12, y, 31, y + 19, GRAY);
            r->fill(dc, 14, y + 2, 29, y + 17, 0);
            if (developer == i)
                r->fill(dc, 18, y + 6, 25, y + 13, BLUE);
            r->text(dc, 40, y + 2, choices[i], WHITE, 14);
        }
        r->text(dc, 40, 272, "Only install verified apps.", GRAY, 14);
        r->text(dc, 40, 362, "Install apps from other sources.", GRAY, 14);
    } else if (view == 6) {
        r->text(dc, 12, 101, "Device encryption", WHITE, 22);
        r->text(dc, 12, 148, "Device encryption helps protect your files", WHITE, 14);
        r->text(dc, 12, 169, "and folders from unauthorized access.", WHITE, 14);
        toggle(dc, r, 213, encryption);
        r->text(dc, 12, 267, "Preview only; storage is not encrypted.", GRAY, 12);
    } else if (view == 7) {
        r->text(dc, 12, 101, "Update status", WHITE, 22);
        r->text(dc, 12, 148, "No updates are available.", WHITE, 14);
        r->fill(dc, 12, 216, 172, 248, RGB_(46, 46, 46));
        r->text(dc, 23, 225, "Check for updates", WHITE, 14);
        r->text(dc, 12, 333, "Update settings", WHITE, 22);
        r->text(dc, 12, 382, "Updates are not installed in this demo.", GRAY, 14);
        r->text(dc, 12, 435, "Change active hours", BLUE, 14);
        r->text(dc, 12, 478, "Restart options", BLUE, 14);
    } else {
        r->text(dc, 12, 101, view == 8 ? leaf : names[category], WHITE, 22);
    }
    if (view == 0) {
        int max = 9 * 64 - 396;
        if (scroll_y < max)
            r->fill(dc, 316, 124 + scroll_y / 2, 319, 224 + scroll_y / 2, GRAY);
    }

    r->clip(0, 0, 320, 528);
}
static int limit(void)
{
    int height = view == 0 ? 708 : view == 1 ? 522 : 0;
    return height > content_bottom ? height - content_bottom : 0;
}
static int input(U32 msg, U32 wp, U32 lp)
{
    int x = (int)(short)(lp & 65535u), y = (int)(short)(lp >> 16);
    if (msg == WM_KEY_) {
        tracking = 0;
        if (wp == 0x1b || wp == 0x25) {
            if (view == 0)
                return -1;
            view = view == 2 || (view == 8 && category == 0) ? 1 : view >= 5 ? 4 : 0;
            scroll_y = 0;
            return 1;
        }
        if (wp == 0x71) {
            search = 1;
            return 1;
        }
        if (wp == 0x26 || wp == 0x28) {
            scroll_y = clampi(scroll_y + (wp == 0x28 ? 48 : -48), 0, limit());
            return 1;
        }
        if (search && wp == 8) {
            if (len)
                query[--len] = 0;
            return 1;
        }
        if (wp == 13) {
            search = 0;
            CALL(SipShowIM, (0));
            return 1;
        }
    }
    if ((msg == WM_CHAR_ || msg == WM_KEY_) && search && wp >= 32 && wp < 127 && len < 35) {
        query[len++] = (char)wp;
        query[len] = 0;
        scroll_y = 0;
        return 1;
    }
    if (msg == WM_MOVE_ && tracking) {
        int delta = y - drag_y;
        if (delta > 8 || delta < -8)
            moved = 1;
        if (moved)
            scroll_y = clampi(drag_scroll + drag_y - y, 0, limit());
        return 1;
    }
    if (msg == WM_DOWN_) {
        if (tracking) {
            int delta = y - drag_y;
            if (delta > 8 || delta < -8) {
                moved = 1;
                scroll_y = clampi(drag_scroll + drag_y - y, 0, limit());
            }
            return 1;
        }
        tracking = 1;
        moved = 0;
        drag_y = y;
        drag_scroll = scroll_y;
        if (y >= 24 && y < 72 && x < 44) {
            view = scroll_y = search = len = 0;
            query[0] = 0;
            CALL(SipShowIM, (0));
            tracking = 0;
            return 1;
        }
        if (view == 0 || view == 1 || view == 4) {
            if (y >= 80 && y < 112) {
                search = 1;
                CALL(SipShowIM, (1));
                tracking = 0;
            }
            return 1;
        }
        if (view == 2) {
            if (y >= 168 && y <= 198)
                brightness = clampi(div_((x - 12) * 100, 276), 0, 100);
            else if (y >= 252 && y <= 282)
                automatic = !automatic;
            else if (y >= 322 && y <= 352)
                rotation = !rotation;
            else if (y >= 397 && y <= 429)
                scale = clampi(div_((x - 12) * 100, 276), 0, 100);
            else if (y >= 439 && y <= 471) {
                if (x >= 12 && x < 102) {
                    saved_brightness = brightness;
                    saved_automatic = automatic;
                    saved_rotation = rotation;
                    saved_scale = scale;
                } else if (x >= 122 && x < 212) {
                    brightness = saved_brightness;
                    automatic = saved_automatic;
                    rotation = saved_rotation;
                    scale = saved_scale;
                }
            }
        }
        if (view == 5 && y >= 238 && y < 508)
            developer = clampi(div_(y - 238, 90), 0, 2);
        if (view == 6 && y >= 207 && y < 246)
            encryption = !encryption;
        return 1;
    }
    if (msg == WM_UP_) {
        int valid = tracking && !moved && y - drag_y <= 8 && drag_y - y <= 8;
        tracking = 0;
        if (!valid)
            return 1;
        if (view == 0 && y >= 132) {
            int selected = div_(y - 132 + scroll_y, 64), row = 0;
            for (unsigned i = 0; i < 9; ++i)
                if (matches(names[i]) && row++ == selected) {
                    category = (int)i;
                    view = i == 0 ? 1 : i == 8 ? 4 : 3;
                    break;
                }
        } else if ((view == 1 || view == 4) && y >= 186) {
            int row = div_(y - 186 + scroll_y, 48);
            if (view == 1 && row < 7) {
                leaf = system_names[row];
                view = row == 0 ? 2 : 8;
            } else if (view == 4 && row < 6) {
                leaf = update_names[row];
                view = row == 0 ? 7 : row == 2 ? 6 : row == 4 ? 5 : 8;
            }
        }
        scroll_y = search = len = 0;
        query[0] = 0;
        CALL(SipShowIM, (0));
        return 1;
    }
    return 0;
}
static void reset(void)
{
    view = category = scroll_y = search = len = tracking = moved = 0;
    query[0] = 0;
}
static int page(void)
{
    return view;
}
static const Settings settings = {PROFILE, paint, input, reset, page, bounds};
const Settings *SettingsGetView(void)
{
    return &settings;
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
