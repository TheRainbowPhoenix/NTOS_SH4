/* uixctl.c — shared, themed control painters for the UIX profile.
 *
 * One implementation of the W10M control set (button, checkbox, toggle
 * switch, text field), reused by the Settings mock, the Edge port and any
 * future app. Geometry follows the reference captures
 * (edge/analysis/EDGE_UI_REFERENCE.md): 20px checkbox with accent fill and
 * a white check, 2px #CCCCCC border unchecked; flat 32px buttons; W10M
 * toggle; bordered input field with accent caret.
 *
 * Colours come from the renderer's theme table (Render.theme) so the same
 * code draws light controls under uixrender and dark controls under the
 * Edge host. Compiles standalone (no heap, no statics besides the light
 * fallback table).
 */
#include "phone.h"
#include "font_bitmap.h"

IMP(const Render *, render_SpGetThreadContext, (void))

/* Light fallback: matches the original phone palette exactly, so the
 * settings-app look is unchanged when the renderer has no theme. */
static const U32 light_theme[UIX_TH_COUNT] = {
    SURFACE,                /* UIX_TH_SURFACE   */
    CARD,                   /* UIX_TH_KEY       */
    PANEL,                  /* UIX_TH_KEY_ALT   */
    INK,                    /* UIX_TH_INK       */
    CARD,                   /* UIX_TH_KEY_GO    */
    INK,                    /* UIX_TH_GO_INK    */
    BLUE,                   /* UIX_TH_ACCENT    */
    RGB_(195, 215, 232),    /* UIX_TH_CTRL_BG   */
    INK,                    /* UIX_TH_CTRL_INK  */
    RGB_(124, 142, 158),    /* UIX_TH_CTRL_EDGE */
    RGB_(124, 142, 158),    /* UIX_TH_FIELD_EDGE */
};

static U32 th(const Render *r, int slot)
{
    if (r && r->theme)
        return r->theme[slot];
    return light_theme[slot];
}

static const Render *ctx(void)
{
    return CALL(render_SpGetThreadContext, ());
}

static unsigned txt_w(const Render *r, const char *s, int h)
{
    (void)r;
    /* real metrics from the shared baked font (font_bitmap.h) */
    const PhoneFont *font = &phone_fonts[0];
    int best = 999;
    for (unsigned i = 0; i < sizeof(phone_fonts) / sizeof(phone_fonts[0]); ++i) {
        const PhoneFont *f = &phone_fonts[i];
        int d = f->height - h;
        if (d < 0)
            d = -d;
        if (d < best) {
            font = f;
            best = d;
        }
    }
    int w = 0;
    for (; s && *s; ++s) {
        unsigned ch = (unsigned char)*s;
        if (ch < (unsigned)font->first || ch >= (unsigned)font->first + font->count)
            ch = '?';
        w += font->glyphs[ch - (unsigned)font->first].advance + 1;
    }
    return (unsigned)w;
}

/* Flat W10M button: solid fill, centred label, 1px pressed nudge. */
void ctl_button(void *dc, const Render *r, int x, int y, int w, int h,
                const char *label, int pressed)
{
    if (!r || !label)
        return;
    U32 bg = th(r, UIX_TH_CTRL_BG);
    if (pressed) {
        U32 d = RGB_(((bg & 255) * 3) / 4, (((bg >> 8) & 255) * 3) / 4,
                     (((bg >> 16) & 255) * 3) / 4);
        r->fill(dc, x + 1, y + 1, x + w, y + h, d);
    } else {
        r->fill(dc, x, y, x + w, y + h, bg);
    }
    unsigned tw = txt_w(r, label, 15);
    int tx = x + ((int)(w - (int)tw) >> 1);
    if (pressed)
        ++tx;
    r->text(dc, tx, y + ((h - 15) >> 1) + (pressed ? 1 : 0), label,
            th(r, UIX_TH_CTRL_INK), 15);
}

/* W10M checkbox: 20px box at (x,y); label right of it, vertically centred.
 * Checked = accent fill + white check glyph; unchecked = 2px grey border. */
void ctl_checkbox(void *dc, const Render *r, int x, int y, int checked,
                  const char *label)
{
    if (!r)
        return;
    if (checked) {
        r->fill(dc, x, y, x + 20, y + 20, th(r, UIX_TH_ACCENT));
        /* white check: two strokes meeting at the box centre */
        r->fill(dc, x + 4, y + 10, x + 6, y + 12, th(r, UIX_TH_INK));
        r->fill(dc, x + 6, y + 12, x + 8, y + 14, th(r, UIX_TH_INK));
        r->fill(dc, x + 8, y + 8, x + 10, y + 14, th(r, UIX_TH_INK));
        r->fill(dc, x + 10, y + 6, x + 12, y + 12, th(r, UIX_TH_INK));
        r->fill(dc, x + 12, y + 4, x + 14, y + 10, th(r, UIX_TH_INK));
        r->fill(dc, x + 14, y + 2, x + 16, y + 8, th(r, UIX_TH_INK));
    } else {
        U32 e = th(r, UIX_TH_CTRL_EDGE);
        r->fill(dc, x, y, x + 20, y + 2, e);
        r->fill(dc, x, y + 18, x + 20, y + 20, e);
        r->fill(dc, x, y, x + 2, y + 20, e);
        r->fill(dc, x + 18, y, x + 20, y + 20, e);
    }
    if (label)
        r->text(dc, x + 29, y + 2, label, th(r, UIX_TH_CTRL_INK), 15);
}

/* W10M toggle switch: 44x20 pill, white knob, accent track when on. */
void ctl_toggle(void *dc, const Render *r, int x, int y, int on)
{
    if (!r)
        return;
    U32 track = on ? th(r, UIX_TH_ACCENT) : th(r, UIX_TH_CTRL_EDGE);
    U32 knob = WHITE;
    /* track: rectangle + rounded end caps */
    r->fill(dc, x + 6, y, x + 38, y + 20, track);
    r->ellipse(dc, x + 6, y + 10, 6, 10, track, 0);
    r->ellipse(dc, x + 38, y + 10, 6, 10, track, 0);
    /* knob */
    int kx = on ? x + 30 : x + 2;
    r->fill(dc, kx + 2, y + 2, kx + 10, y + 18, knob);
    r->ellipse(dc, kx + 6, y + 10, 4, 8, knob, 0);
}

/* Input field: bordered box, hint text grey, live text in ink, blue caret
 * at the text tail when focused. */
void ctl_field(void *dc, const Render *r, int x, int y, int w, int h,
               const char *text, const char *hint, int focused)
{
    if (!r)
        return;
    U32 border = th(r, UIX_TH_FIELD_EDGE);
    if (focused)
        border = th(r, UIX_TH_ACCENT);
    /* 1px border on the page background (parent paints the fill) */
    r->fill(dc, x, y, x + w, y + 1, border);
    r->fill(dc, x, y + h - 1, x + w, y + h, border);
    r->fill(dc, x, y, x + 1, y + h, border);
    r->fill(dc, x + w - 1, y, x + w, y + h, border);
    const char *show = (text && *text) ? text : hint;
    if (show && *show) {
        U32 c = (text && *text) ? th(r, UIX_TH_CTRL_INK) : th(r, UIX_TH_FIELD_EDGE);
        r->text(dc, x + 8, y + ((h - 15) >> 1), show, c, 15);
        if (focused && text && *text) {
            int tw = (int)txt_w(r, text, 15);
            if (x + 8 + tw + 4 < x + w - 4)
                r->fill(dc, x + 8 + tw + 2, y + 4, x + 10 + tw, y + h - 4,
                        th(r, UIX_TH_ACCENT));
        }
    } else if (focused) {
        r->fill(dc, x + 8, y + 4, x + 10, y + h - 4, th(r, UIX_TH_ACCENT));
    }
}

/* ---- UXCTL.DLL vtable (device shared-controls model) ----
 * Static-link hosts (e.g. the Edge app) compile with -DUXCTL_NO_FACTORY
 * and call ctl_* directly; device apps import the Controls vtable the
 * same way they import the keyboard. */
#ifndef UXCTL_NO_FACTORY
static void v_button(void *dc, int x, int y, int w, int h, const char *label,
                     int pressed)
{
    ctl_button(dc, ctx(), x, y, w, h, label, pressed);
}
static void v_checkbox(void *dc, int x, int y, int checked, const char *label)
{
    ctl_checkbox(dc, ctx(), x, y, checked, label);
}
static void v_toggle(void *dc, int x, int y, int on)
{
    ctl_toggle(dc, ctx(), x, y, on);
}
static void v_field(void *dc, int x, int y, int w, int h, const char *text,
                    const char *hint, int focused)
{
    ctl_field(dc, ctx(), x, y, w, h, text, hint, focused);
}
static const Controls uixctl_vtable = {PROFILE, 0, v_button, v_checkbox,
                                       v_toggle, v_field};
#ifdef NTOS_LINUX_BUILD
__attribute__((dllexport))
#endif
const Controls *UIXGetSchemaFactory(void)
{
    return &uixctl_vtable;
}
#ifdef NTOS_LINUX_BUILD
__attribute__((dllexport))
#endif
void UIXSchemaUnloaded(void)
{
}
int phone_unsupported(void)
{
    return NOTIMPL;
}
#ifdef NTOS_LINUX_BUILD
int DllMainCRTStartup_impl(void *h, U32 r, void *r2)
{
    (void)h;
    (void)r;
    (void)r2;
    return 1;
}
__asm__(".globl _DllMainCRTStartup\n_DllMainCRTStartup = DllMainCRTStartup_impl\n");
#else
#ifndef PHONE_HOST
int main(void)
{
    return 1;
}
#endif
#endif
#endif /* !UXCTL_NO_FACTORY */
