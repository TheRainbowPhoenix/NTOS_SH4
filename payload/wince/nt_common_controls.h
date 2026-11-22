/* NTOS GWES bounded native common-control state, CP400 / SH4 no FPU.
 * This is the process-local USER part of a future kernel-owned HWND service.
 * Tokens are monotonically allocated (no stale-pointer alias on slot reuse).
 * A control never owns a framebuffer. COREDLL paints controls after the
 * app's WM_PAINT, before modal dialogs and the system SIP.
 */
#ifndef NTOS_COMMON_CONTROLS_H
#define NTOS_COMMON_CONTROLS_H

#define NT_CTL_MAX 8u
#define NT_CTL_LIST_MAX 8u
#define NT_CTL_LIST_TEXT 32u
#define NT_CTL_TEXT_MAX 64u
#define NT_CTL_HANDLE_TAG 0x46000000u
#define NT_CTL_STATIC 1u
#define NT_CTL_BUTTON 2u
#define NT_CTL_CHECKBOX 3u
#define NT_CTL_PROGRESS 4u
#define NT_CTL_LISTBOX 5u
#define NT_CTL_COMBOBOX 6u

typedef struct {
    u32 handle, id, role, style;
    void *parent;
    NT_RECT bounds;
    u16 title[NT_CTL_TEXT_MAX];
    u16 lines[NT_CTL_LIST_MAX][NT_CTL_LIST_TEXT];
    unsigned line_count, selected;
    unsigned value, minimum, maximum;
    unsigned visible, enabled, focus, checked;
} NT_COMMON_CTL;
static NT_COMMON_CTL g_common_controls[NT_CTL_MAX];
static u32 g_control_serial = 1u;

static unsigned nt_ctl_name(const u16 *name, const char *literal)
{
    if (!name)
        return 0;
    unsigned i = 0;
    while (literal[i] && i < 32u) {
        u32 ch = name[i], want = (unsigned char)literal[i];
        if (ch >= 'A' && ch <= 'Z')
            ch += 32u;
        if (want >= 'A' && want <= 'Z')
            want += 32u;
        if (ch != want)
            return 0;
        ++i;
    }
    return !name[i] && !literal[i];
}
static unsigned nt_ctl_kind(const u16 *name, u32 style)
{
    if (nt_ctl_name(name, "STATIC"))
        return NT_CTL_STATIC;
    if (nt_ctl_name(name, "BUTTON"))
        return (style & 15u) == 2u || (style & 15u) == 3u ||
               (style & 15u) == 5u || (style & 15u) == 6u ?
                   NT_CTL_CHECKBOX : NT_CTL_BUTTON;
    if (nt_ctl_name(name, "msctls_progress32"))
        return NT_CTL_PROGRESS;
    if (nt_ctl_name(name, "LISTBOX"))
        return NT_CTL_LISTBOX;
    if (nt_ctl_name(name, "COMBOBOX"))
        return NT_CTL_COMBOBOX;
    return 0;
}
static NT_COMMON_CTL *nt_ctl_find(void *handle)
{
    u32 token = (u32)(uintptr_t)handle;
    if ((token & 0xFF000000u) != NT_CTL_HANDLE_TAG || token == NT_CTL_HANDLE_TAG)
        return 0;
    for (unsigned i = 0; i < NT_CTL_MAX; ++i)
        if (g_common_controls[i].handle == token)
            return &g_common_controls[i];
    return 0;
}
static void nt_ctl_copy(u16 *dst, unsigned capacity, const u16 *src)
{
    unsigned i = 0;
    if (src)
        while (i + 1u < capacity && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    dst[i] = 0;
}
static NT_COMMON_CTL *nt_ctl_create(unsigned kind, void *parent, u32 id,
                                    u32 style, int x, int y, int width,
                                    int height, const u16 *title)
{
    if (!kind || !parent || width < 1 || height < 1 ||
        x < 0 || y < 0 || x >= 320 || y >= 528 ||
        g_control_serial > 0x00FFFFFFu)
        return 0;
    for (unsigned i = 0; i < NT_CTL_MAX; ++i) {
        NT_COMMON_CTL *c = &g_common_controls[i];
        if (c->handle)
            continue;
        if (width > 320 - x)
            width = 320 - x;
        if (height > 528 - y)
            height = 528 - y;
        /* Explicit bounded reset avoids the SH4 freestanding mem helpers. */
        volatile unsigned char *bytes = (volatile unsigned char *)c;
        for (unsigned n = 0; n < sizeof(*c); ++n)
            bytes[n] = 0u;
        c->handle = NT_CTL_HANDLE_TAG | g_control_serial++;
        c->id = id;
        c->role = kind;
        c->style = style;
        c->parent = parent;
        c->bounds.left = x;
        c->bounds.top = y;
        c->bounds.right = x + width;
        c->bounds.bottom = y + height;
        c->visible = 0;
        c->enabled = 1;
        c->selected = 0xFFFFFFFFu;
        c->minimum = 0;
        c->maximum = 100;
        nt_ctl_copy(c->title, NT_CTL_TEXT_MAX, title);
        return c;
    }
    return 0;
}
static int nt_ctl_destroy(void *handle)
{
    NT_COMMON_CTL *c = nt_ctl_find(handle);
    if (!c)
        return 0;
    c->handle = 0;
    c->visible = 0;
    c->focus = 0;
    c->parent = 0;
    return 1;
}
static void nt_ctl_destroy_parent(void *parent)
{
    for (unsigned i = 0; i < NT_CTL_MAX; ++i)
        if (g_common_controls[i].handle && g_common_controls[i].parent == parent)
            nt_ctl_destroy((void *)(uintptr_t)g_common_controls[i].handle);
}
static NT_COMMON_CTL *nt_ctl_hit(void *parent, int x, int y)
{
    /* Controls created last paint on top, and own their hit-test region. */
    for (int i = (int)NT_CTL_MAX - 1; i >= 0; --i) {
        NT_COMMON_CTL *c = &g_common_controls[i];
        if (!c->handle || !c->visible || !c->enabled || c->parent != parent)
            continue;
        if (x >= c->bounds.left && x < c->bounds.right &&
            y >= c->bounds.top && y < c->bounds.bottom)
            return c;
    }
    return 0;
}
static int nt_ctl_command(NT_COMMON_CTL *c)
{
    if (!c || !c->visible || !c->enabled)
        return 0;
    if (c->role == NT_CTL_CHECKBOX)
        c->checked ^= 1u;
    return c->role == NT_CTL_BUTTON || c->role == NT_CTL_CHECKBOX;
}
static void nt_ctl_focus(NT_COMMON_CTL *focused)
{
    for (unsigned i = 0; i < NT_CTL_MAX; ++i)
        g_common_controls[i].focus = 0;
    if (focused)
        focused->focus = 1;
}
static int nt_ctl_count(NT_COMMON_CTL *c)
{
    return c && (c->role == NT_CTL_LISTBOX || c->role == NT_CTL_COMBOBOX) ?
           (int)c->line_count : -1;
}
static int nt_ctl_add_string(NT_COMMON_CTL *c, const u16 *s)
{
    if (nt_ctl_count(c) < 0 || !s || c->line_count >= NT_CTL_LIST_MAX)
        return -1;
    unsigned index = c->line_count++;
    nt_ctl_copy(c->lines[index], NT_CTL_LIST_TEXT, s);
    return (int)index;
}
static int nt_ctl_select(NT_COMMON_CTL *c, int index)
{
    if (nt_ctl_count(c) < 0)
        return -1;
    if (index == -1) {
        c->selected = 0xFFFFFFFFu;
        return -1;
    }
    if (index < 0 || (unsigned)index >= c->line_count)
        return -1;
    c->selected = (unsigned)index;
    return index;
}
#endif
