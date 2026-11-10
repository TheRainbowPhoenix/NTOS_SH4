/* Small native UIX profile. Private tables are NOT Microsoft's UIX COM ABI.
 * All public module names/ordinals come from the reference PE manifests. */
#ifndef PHONE_H
#define PHONE_H
#include <stdint.h>
#include "ntimport.h"
typedef uint32_t U32;
typedef uint16_t W16;
typedef struct {
    int left, top, right, bottom;
} Rect;
typedef struct {
    int x, y;
} Pt;
#define RGB_(r, g, b) ((U32)((r) | ((g) << 8) | ((b) << 16)))
#define WHITE RGB_(255, 255, 255)
#define BLUE RGB_(40, 121, 181)
/* NTOS light-default palette: low-glare ivory/slate surfaces with muted blue. */
#define INK RGB_(29, 42, 58)
#define NAV_INK RGB_(12, 16, 22)
#define SURFACE RGB_(242, 247, 251)
#define PANEL RGB_(218, 233, 244)
#define CARD RGB_(231, 241, 249)
#define MUTED RGB_(101, 120, 138)
#define GRAY RGB_(124, 142, 158)
#define PROFILE 0x55495831u
#define NOTIMPL ((int)0x80004001u)
#define WM_PAINT_ 15u
#define WM_KEY_ 0x100u
#define WM_DOWN_ 0x201u
#define WM_UP_ 0x202u
#define WM_MOVE_ 0x200u
#define WM_CHAR_ 0x102u
/* Shared theme slots. A renderer may publish a table; controls fall back
 * to the light palette when the host renderer predates theming (NULL).
 * This mirrors W10M: the shell keyboard/controls pick system-theme
 * colours, so one implementation serves light (Settings) and dark (Edge)
 * hosts. */
enum {
    UIX_TH_SURFACE = 0, /* keyboard backdrop / field track */
    UIX_TH_KEY,         /* normal key */
    UIX_TH_KEY_ALT,     /* shift, &123, modifier keys */
    UIX_TH_INK,         /* text on keys/controls */
    UIX_TH_KEY_GO,      /* enter/search accent key fill */
    UIX_TH_GO_INK,      /* glyph on the accent key */
    UIX_TH_ACCENT,      /* checked fill, focused caret, links */
    UIX_TH_CTRL_BG,     /* button/check strip background */
    UIX_TH_CTRL_INK,    /* button label */
    UIX_TH_CTRL_EDGE,   /* unchecked border / field border */
    UIX_TH_COUNT
};
typedef struct Render {
    U32 profile;
    void (*clip)(int, int, int, int);
    void (*fill)(void *, int, int, int, int, U32);
    void (*text)(void *, int, int, const char *, U32, int);
    void (*line)(void *, int, int, int, int, U32);
    void (*ellipse)(void *, int, int, int, int, U32, int);
    void (*polygon)(void *, const Pt *, unsigned, U32);
    void (*surface)(int);
    const U32 *theme; /* UIX_TH_* table or NULL for light defaults */
} Render;
typedef struct Controls {
    U32 profile;
    void (*icon)(void *, int, int, int, int, U32);
} Controls;

/* Shared control painters (uixctl.c): themed, geometry measured from the
 * W10M reference captures. Any app (Settings mock, Edge, future ports)
 * links or imports these instead of re-drawing buttons/checks/toggles. */
void ctl_button(void *dc, const Render *r, int x, int y, int w, int h,
                const char *label, int pressed);
void ctl_checkbox(void *dc, const Render *r, int x, int y, int checked,
                  const char *label);
void ctl_toggle(void *dc, const Render *r, int x, int y, int on);
void ctl_field(void *dc, const Render *r, int x, int y, int w, int h,
               const char *text, const char *hint, int focused);
typedef struct Chrome {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*expanded)(void);
    int (*navheight)(void);
    int (*navigation)(int);
} Chrome;
typedef struct Menu {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*page)(void);
    int (*scroll)(void);
    void (*open)(unsigned);
    void (*bounds)(int);
} Menu;
typedef struct Keyboard {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    int (*page)(void);
    void (*dock)(int);
} Keyboard;
typedef struct Settings {
    U32 profile;
    void (*paint)(void *);
    int (*input)(U32, U32, U32);
    void (*reset)(void);
    int (*page)(void);
    void (*bounds)(int);
} Settings;
typedef struct {
    U32 size, flags;
    Rect desktop, sip;
    U32 data_size;
    void *data;
} SipInfo;
#define MAX_APPS 40
#define ICON_BYTES 512
typedef struct {
    unsigned short id;
    unsigned char size, flags;
    U32 color;
    char name[40], path[96];
    unsigned short icon_len;
    unsigned char icon[ICON_BYTES];
    U32 pe_icon_offset, pe_icon_size;
    unsigned char pe_icon_type;
} AppEntry;
typedef struct {
    unsigned short id;
    unsigned char size, x, y, reserved;
} Pin;
typedef struct Catalog {
    unsigned count, pin_count;
    AppEntry apps[MAX_APPS];
    Pin pins[MAX_APPS];
} Catalog;
#define APP_SETTINGS_ID 21u
/* RunApplication receives the 0x48-byte configuration seen in StartHost.
 * SH4 BE pointers replace x86 pointers; narrow UI tables stay private. */
typedef struct AppConfig {
    const W16 *root, *page;
    U32 reserved[16];
} AppConfig;
typedef struct App {
    U32 profile;
    const W16 *title;
    const Menu *menu;
    const Chrome *chrome;
} App;
/* On-device: grouped, real PE IAT slots. Native QA: direct typed calls. */
#ifdef PHONE_HOST
#define IMP(ret, alias, params) extern ret alias params;
#define CALL(alias, args) alias args
#else
#define IMP(ret, alias, params) NT_IMPORT(ret, alias, params) NT_DEFINE(alias)
#define CALL(alias, args) NT_CALL(alias, args)
#endif
enum {
    ICON_PHONE,
    ICON_MESSAGE,
    ICON_EDGE,
    ICON_MAIL,
    ICON_PEOPLE,
    ICON_CORTANA,
    ICON_PHOTO,
    ICON_STORE,
    ICON_MUSIC,
    ICON_VIDEO,
    ICON_WEATHER,
    ICON_NEWS,
    ICON_MONEY,
    ICON_CALC,
    ICON_MAP,
    ICON_WORD,
    ICON_POWERPOINT,
    ICON_EXCEL,
    ICON_NOTE,
    ICON_ALARM,
    ICON_CAMERA,
    ICON_SUPPORT,
    ICON_SETTINGS,
    ICON_SEARCH
};
static inline int clampi(int n, int a, int b)
{
    return n < a ? a : n > b ? b : n;
}
/* No libgcc division helpers and no floating-point on this target. */
static inline unsigned udiv_(unsigned n, unsigned d)
{
    unsigned q = 0, r = 0;
    if (!d)
        return 0;
    for (int i = 31; i >= 0; --i) {
        unsigned c = r >> 31;
        r = (r << 1) | ((n >> (unsigned)i) & 1u);
        if (c || r >= d) {
            r -= d;
            q |= 1u << (unsigned)i;
        }
    }
    return q;
}
static inline int div_(int a, int b)
{
    unsigned n = a < 0 ? 0u - (unsigned)a : (unsigned)a;
    unsigned d = b < 0 ? 0u - (unsigned)b : (unsigned)b;
    unsigned q;
    if (d && (d & (d - 1u)) == 0u) {
        /* SVF coordinates use 16/32/64-unit viewports extensively.
         * A shift avoids the 32-round software divider per coordinate. */
        unsigned shift = 0;
        while ((1u << shift) != d)
            ++shift;
        q = n >> shift;
    } else {
        q = udiv_(n, d);
    }
    return (a < 0) ^ (b < 0) ? -(int)q : (int)q;
}
#endif
