/* Prepared native Settings views; values are local previews, not hardware configuration. */
#include "phone.h"
IMP(const Render *, render_SpGetThreadContext, (void))
IMP(int, svf_SVFDrawTint, (void *, const void *, unsigned, int, int, int, int, U32))
IMP(int, SipShowIM, (U32))
#include "settings_icons.h"
static int content_bottom = 528;
static void bounds(int b)
{
    content_bottom = b;
}
static int view, category, scroll_y, drag_y, drag_scroll, search, len, tracking, moved;
static int slider_drag, pressed_button, pressed_row = -1, scale_dropdown;
static const char *leaf = "Settings";
static int developer = 2, encryption;
static int brightness = 50, automatic = 1, rotation, scale = 100;
static int saved_brightness = 50, saved_automatic = 1, saved_rotation, saved_scale = 100;
static char query[40];
static const char *names[] = {"System",          "Devices",  "Network & wireless",
                              "Personalization", "Accounts", "Time & language",
                              "Gaming",          "Privacy",  "Update & security"};
static const char *details[] = {"Display, notifications, battery",
                                "Bluetooth, camera",
                                "Wi-Fi, airplane mode, cellular",
                                "Start, lock screen, sounds",
                                "Your accounts, email, sync",
                                "Speech, region, keyboard",
                                "Game bar, captures, controllers",
                                "Location, feedback",
                                "Backup, Find My Phone"};
static const char *system_names[] = {
    "Display",     "Notifications & actions", "Phone", "Messaging", "Storage", "Battery",
    "Offline maps", "About"};
static const char *update_names[] = {"Phone update",  "Backup",         "Device encryption",
                                     "Find My Phone", "For developers", "Windows Insider Program"};
static void icon(void *dc, unsigned which, int x, int y, int size)
{
    const SettingsIcon *i = &settings_icons[which];
    CALL(svf_SVFDrawTint, (dc, i->data, i->length, x, y, size, size, RGB_(0, 0, 0)));
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
    r->fill(dc, 14, y + 2, 54, y + 18, on ? BLUE : CARD);
    r->fill(dc, on ? 40 : 16, y + 4, on ? 52 : 28, y + 16, WHITE);
    r->text(dc, 68, y + 2, on ? "On" : "Off", INK, 14);
}
static void flat_button(void *dc, const Render *r, int x, int y, int width, const char *label,
                        int pressed)
{
    U32 fill = pressed ? RGB_(160, 190, 216) : RGB_(195, 215, 232);
    r->fill(dc, x + pressed, y + pressed, x + width - pressed, y + 32 - pressed, fill);
    r->text(dc, x + 14 + pressed, y + 9 + pressed, label, INK, 14);
}
static int slider_value(int x)
{
    /* At most 21 full-screen paints across a slider sweep, rather than
     * a paint for every touch pixel (~276). Display settings are previews. */
    int value = clampi(div_((x - 12) * 100, 276), 0, 100);
    return ((value + 2) / 5) * 5;
}
static int slider_update(int x)
{
    int next = slider_value(x);
    int *current = slider_drag == 1 ? &brightness : &scale;
    if (next == *current)
        return 0; /* The caller skips invalidation for unchanged values. */
    *current = next;
    return 1;
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
/* Search is global across Settings levels, not restricted to the category
 * currently displayed. The results are static navigation actions. */
typedef struct {
    const char *title;
    int target;
} SearchResult;
static const SearchResult search_items[] = {
    {"Display", 2}, {"About", 9}, {"System", 1},
    {"Gaming", 3}, {"Personalization", 3},
    {"Device encryption", 6}, {"For developers", 5},
    {"Phone update", 7}, {"Network & wireless", 3},
    {"Time & language", 3}, {"Privacy", 3}
};
static void search_results(void *dc, const Render *r)
{
    if (!search || !len)
        return;
    r->clip(0, 116, 320, content_bottom);
    r->fill(dc, 8, 116, 312, content_bottom, RGB_(237, 243, 249));
    unsigned shown = 0;
    for (unsigned i = 0; i < sizeof(search_items) / sizeof(search_items[0]); ++i) {
        if (!matches(search_items[i].title))
            continue;
        int y = 120 + (int)shown * 48;
        if (y + 48 > content_bottom)
            break;
        if (pressed_row == (int)i)
            r->fill(dc, 10, y, 310, y + 46, RGB_(187, 200, 215));
        r->text(dc, 23, y + 14, search_items[i].title, INK, 14);
        ++shown;
    }
    if (!shown)
        r->text(dc, 23, 143, "No matching settings", MUTED, 14);
}
static int search_hit(int y)
{
    if (y < 120)
        return -1;
    int row = (y - 120) / 48;
    unsigned shown = 0;
    for (unsigned i = 0; i < sizeof(search_items) / sizeof(search_items[0]); ++i) {
        if (!matches(search_items[i].title))
            continue;
        if ((int)shown++ == row)
            return (int)i;
    }
    return -1;
}
static void dropdown(void *dc, const Render *r)
{
    static const char *options[] = {"100% (Recommended)", "125%", "150%", "175%"};
    int selected = clampi((scale - 100) / 25, 0, 3);
    r->fill(dc, 12, 397, 293, 432, RGB_(133, 145, 157));
    r->fill(dc, 14, 399, 291, 430, RGB_(223, 229, 236));
    r->text(dc, 23, 407, options[selected], INK, 14);
    r->line(dc, 273, 412, 280, 419, INK);
    r->line(dc, 280, 419, 287, 412, INK);
    if (!scale_dropdown)
        return;
    r->fill(dc, 12, 230, 293, 393, RGB_(133, 145, 157));
    for (int i = 0; i < 4; ++i) {
        int y = 232 + 40 * i;
        r->fill(dc, 14, y, 291, y + 40,
                i == selected ? RGB_(181, 190, 201) : RGB_(242, 246, 250));
        r->text(dc, 23, y + 12, options[i], INK, 14);
    }
}
static void paint(void *dc)
{
    const Render *r = CALL(render_SpGetThreadContext, ());
    r->clip(0, 24, 320, 528);
    r->fill(dc, 0, 24, 320, 528, view >= 2 && view != 4 ? SURFACE : PANEL);
    r->fill(dc, 0, 24, 320, 72, PANEL);
    icon(dc, 8, 12, 36, 24);
    r->text(dc, 48, 40,
            view == 2   ? "Display"
            : view == 5 ? "For developers"
            : view == 6 ? "Device encryption"
            : view == 7 ? "Phone update"
            : view == 9 ? "About"
            : view == 8 ? leaf
                        : "Settings",
            INK, 14);
    if (view == 0 || view == 1 || view == 4 || search) {
        r->fill(dc, 12, 80, 308, 112, BLUE);
        r->fill(dc, 14, 82, 306, 110, WHITE);
        r->text(dc, 23, 89, len ? query : search ? "" : "Find a setting", search ? INK : MUTED, 14);
        if (search)
            r->fill(dc, 23 + len * 7, 87, 24 + len * 7, 102, INK);
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
            if (pressed_row == (int)i)
                r->fill(dc, 0, y, 320, y + 58, RGB_(209, 219, 230));
            icon(dc, i, 12, y + 4, 32);
            r->text(dc, 54, y + 3, names[i], INK, 14);
            r->text(dc, 54, y + 24, details[i], GRAY, 12);
        }
    } else if (view == 1) {
        r->text(dc, 12, 143, "System", GRAY, 14);
        r->clip(0, 178, 320, 528);
        for (unsigned i = 0; i < 8; ++i) {
            int y = 186 + (int)i * 48 - scroll_y;
            if (pressed_row == (int)i)
                r->fill(dc, 0, y, 320, y + 46, RGB_(198, 211, 224));
            icon(dc, i == 7 ? 16 : 9 + i, 12, y + 2, 17);
            r->text(dc, 39, y + 4, system_names[i], INK, 14);
        }
    } else if (view == 2) {
        r->text(dc, 12, 98, "Display", INK, 22);
        r->text(dc, 12, 147, "Adjust brightness level", INK, 14);
        slider(dc, r, 181, brightness);
        r->text(dc, 12, 211, "Change brightness automatically when", INK, 14);
        r->text(dc, 12, 232, "lighting changes", INK, 14);
        toggle(dc, r, 257, automatic);
        r->text(dc, 12, 302, "Rotation lock", INK, 14);
        toggle(dc, r, 327, rotation);
        r->text(dc, 12, 371, "Size of text, apps, and items on this display", INK, 14);
        dropdown(dc, r);
        flat_button(dc, r, 12, 439, 90, "Apply", pressed_button == 1);
        flat_button(dc, r, 122, 439, 90, "Cancel", pressed_button == 2);
        r->text(dc, 12, 503, "Show a dot on the connected display when", INK, 14);
    } else if (view == 9) {
        r->text(dc, 12, 105, "Device specifications", INK, 20);
        r->text(dc, 12, 156, "Device", MUTED, 14);
        r->text(dc, 123, 156, "Casio fx-CP400", INK, 14);
        r->text(dc, 12, 193, "Processor", MUTED, 14);
        r->text(dc, 123, 193, "SH4AL-DSP", INK, 14);
        r->text(dc, 12, 230, "Architecture", MUTED, 14);
        r->text(dc, 123, 230, "SuperH, big-endian", INK, 14);
        r->text(dc, 12, 267, "Touch screen", MUTED, 14);
        r->text(dc, 123, 267, "320 x 528", INK, 14);
        r->text(dc, 12, 331, "NTOS specifications", INK, 20);
        r->text(dc, 12, 384, "System", MUTED, 14);
        r->text(dc, 123, 384, "NTOS / WinCE API", INK, 14);
        r->text(dc, 12, 419, "Status", MUTED, 14);
        r->text(dc, 123, 419, "Experimental", INK, 14);
        r->text(dc, 12, 482, "Preview - not Windows 10", MUTED, 12);
    } else if (view == 4) {
        r->text(dc, 12, 143, "Update & security", GRAY, 14);
        r->clip(0, 178, 320, 528);
        for (unsigned i = 0; i < 6; ++i) {
            int y = 186 + (int)i * 48 - scroll_y;
            if (pressed_row == (int)i)
                r->fill(dc, 0, y, 320, y + 46, RGB_(198, 211, 224));
            icon(dc, i == 0 ? 17 : i == 2 ? 7 : 8, 12, y + 2, 17);
            r->text(dc, 39, y + 4, update_names[i], INK, 14);
        }
    } else if (view == 5) {
        r->text(dc, 12, 101, "Use developer features", INK, 22);
        r->text(dc, 12, 148, "These settings are intended for", INK, 14);
        r->text(dc, 12, 169, "development use only.", INK, 14);
        r->text(dc, 12, 199, "Learn more", BLUE, 14);
        static const char *choices[] = {"Windows Store apps", "Sideload apps", "Developer mode"};
        for (int i = 0; i < 3; ++i) {
            int y = 238 + i * 90;
            r->fill(dc, 12, y, 31, y + 19, GRAY);
            r->fill(dc, 14, y + 2, 29, y + 17, SURFACE);
            if (developer == i)
                r->fill(dc, 18, y + 6, 25, y + 13, BLUE);
            r->text(dc, 40, y + 2, choices[i], INK, 14);
        }
        r->text(dc, 40, 272, "Only install verified apps.", GRAY, 14);
        r->text(dc, 40, 362, "Install apps from other sources.", GRAY, 14);
    } else if (view == 6) {
        r->text(dc, 12, 101, "Device encryption", INK, 22);
        r->text(dc, 12, 148, "Device encryption helps protect your files", INK, 14);
        r->text(dc, 12, 169, "and folders from unauthorized access.", INK, 14);
        toggle(dc, r, 213, encryption);
        r->text(dc, 12, 267, "Preview only; storage is not encrypted.", GRAY, 12);
    } else if (view == 7) {
        r->text(dc, 12, 101, "Update status", INK, 22);
        r->text(dc, 12, 148, "No updates are available.", INK, 14);
        flat_button(dc, r, 12, 216, 160, "Check for updates", pressed_button == 3);
        r->text(dc, 12, 333, "Update settings", INK, 22);
        r->text(dc, 12, 382, "Updates are not installed in this demo.", GRAY, 14);
        r->text(dc, 12, 435, "Change active hours", BLUE, 14);
        r->text(dc, 12, 478, "Restart options", BLUE, 14);
    } else {
        r->text(dc, 12, 101, view == 8 ? leaf : names[category], INK, 22);
    }
    if (view == 0) {
        int max = 9 * 64 - 396;
        if (scroll_y < max)
            r->fill(dc, 316, 124 + scroll_y / 2, 319, 224 + scroll_y / 2, GRAY);
    }

    r->clip(0, 0, 320, 528);
    search_results(dc, r);
    r->clip(0, 0, 320, 528);
}
static int limit(void)
{
    int height = view == 0 ? 708 : view == 1 ? 570 : 0;
    return height > content_bottom ? height - content_bottom : 0;
}
static int input(U32 msg, U32 wp, U32 lp)
{
    int x = (int)(short)(lp & 65535u), y = (int)(short)(lp >> 16);
    if (msg == WM_KEY_) {
        tracking = pressed_row = 0;
        if (wp == 0x1b || wp == 0x25) {
            if (view == 0)
                return -1;
            if (search) {
                search = len = 0;
                query[0] = 0;
                CALL(SipShowIM, (0));
                return 1;
            }
            view = view == 2 || view == 9 || (view == 8 && category == 0)
                       ? 1 : view >= 5 && view <= 7 ? 4 : 0;
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
        if (wp == 13 && search && len) {
            int hit = search_hit(120);
            if (hit >= 0) {
                view = search_items[hit].target;
                search = len = 0;
                query[0] = 0;
                CALL(SipShowIM, (0));
                return 1;
            }
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
        if (slider_drag)
            return slider_update(x);
        if (pressed_button || scale_dropdown)
            return 0;
        int delta = y - drag_y;
        if (delta > 8 || delta < -8)
            moved = 1;
        if (moved)
            scroll_y = clampi(drag_scroll + drag_y - y, 0, limit());
        return 1;
    }
    if (msg == WM_DOWN_) {
        if (tracking) {
            if (slider_drag)
                return slider_update(x); /* Some touch drivers repeat WM_DOWN. */
            if (pressed_button)
                return 0;
            int delta = y - drag_y;
            if (delta > 8 || delta < -8) {
                moved = 1;
                scroll_y = clampi(drag_scroll + drag_y - y, 0, limit());
            }
            return 1;
        }
        tracking = 1;
        slider_drag = pressed_button = 0;
        pressed_row = -1;
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
        if (search && len && y >= 120) {
            pressed_row = search_hit(y);
            return 1;
        }
        if (view == 0 || view == 1 || view == 4 || search) {
            if (y >= 80 && y < 112) {
                search = 1;
                CALL(SipShowIM, (1));
                tracking = 0;
            }
            if (!search && view == 0 && y >= 132)
                pressed_row = div_(y - 132 + scroll_y, 64);
            else if (!search && (view == 1 || view == 4) && y >= 186)
                pressed_row = div_(y - 186 + scroll_y, 48);
            return 1;
        }
        if (view == 2) {
            if (scale_dropdown && y >= 232 && y < 392) {
                scale = 100 + clampi((y - 232) / 40, 0, 3) * 25;
                scale_dropdown = 0;
            } else if (y >= 168 && y <= 198) {
                slider_drag = 1;
                brightness = slider_value(x);
            } else if (y >= 252 && y <= 282)
                automatic = !automatic;
            else if (y >= 322 && y <= 352)
                rotation = !rotation;
            else if (y >= 397 && y <= 432) {
                scale_dropdown = !scale_dropdown;
            } else if (y >= 439 && y <= 471) {
                if (x >= 12 && x < 102) {
                    pressed_button = 1;
                    saved_brightness = brightness;
                    saved_automatic = automatic;
                    saved_rotation = rotation;
                    saved_scale = scale;
                } else if (x >= 122 && x < 212) {
                    pressed_button = 2;
                    brightness = saved_brightness;
                    automatic = saved_automatic;
                    rotation = saved_rotation;
                    scale = saved_scale;
                }
            }
        }
        if (view == 7 && y >= 216 && y < 248 && x >= 12 && x < 172)
            pressed_button = 3;
        if (view == 5 && y >= 238 && y < 508)
            developer = clampi(div_(y - 238, 90), 0, 2);
        if (view == 6 && y >= 207 && y < 246)
            encryption = !encryption;
        return 1;
    }
    if (msg == WM_UP_) {
        if (slider_drag || pressed_button) {
            int needs_paint = pressed_button != 0;
            slider_drag = pressed_button = tracking = moved = 0;
            return needs_paint;
        }
        if (search && len && y >= 120) {
            int hit = search_hit(y);
            if (hit >= 0) {
                view = search_items[hit].target;
                if (view == 3)
                    category = 3;
            }
            tracking = 0;
            pressed_row = -1;
            search = len = 0;
            query[0] = 0;
            CALL(SipShowIM, (0));
            return 1;
        }
        int valid = tracking && !moved && y - drag_y <= 8 && drag_y - y <= 8;
        tracking = 0;
        pressed_row = -1;
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
            if (view == 1 && row < 8) {
                leaf = system_names[row];
                view = row == 0 ? 2 : row == 7 ? 9 : 8;
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
    slider_drag = pressed_button = scale_dropdown = 0;
    pressed_row = -1;
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
