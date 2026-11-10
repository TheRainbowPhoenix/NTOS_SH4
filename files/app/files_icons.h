/* files_icons.h — MDL2-style icon set for the W10M FileExplorer port.
 *
 * Same integer-only stroke model as edge/app/icons.c (no TTF on device):
 * every glyph reduces to fill() calls. Names mirror the Segoe MDL2 glyphs
 * the real app uses (U+E752 Up, U+E70E chevron, folder glyph U+E8B7 ...).
 */
#ifndef FILES_ICONS_H
#define FILES_ICONS_H

typedef void (*IconFillFn)(void *dc, int l, int t, int r, int b, unsigned color);

enum {
    FI_NONE = 0,
    FI_HAMBURGER,   /* global nav button (E700)     */
    FI_UP,          /* up one level (E752)          */
    FI_CHEVRON,     /* breadcrumb separator (E70E)  */
    FI_CHEVDOWN,    /* sort bar caret, closed       */
    FI_CHEVUP,      /* sort bar caret, flyout open  */
    FI_FOLDER,      /* amber folder (E8B7)          */
    FI_FOLDER_ADD,  /* new folder (E8F4)            */
    FI_FILE,        /* generic file page (E7C3)     */
    FI_IMAGE,       /* photo (EB9F)                 */
    FI_AUDIO,       /* music note (EC4F)            */
    FI_VIDEO,       /* film (E714)                  */
    FI_DOC,         /* document text (E8A5)         */
    FI_SEARCH,      /* magnifier (E721)             */
    FI_SELECT,      /* multi-select (E73E-ish)      */
    FI_SELECTALL,   /* checklist                    */
    FI_CLEAR,       /* clear selection (X circle)   */
    FI_MORE,        /* "..." ellipsis (E712)        */
    FI_COPY,        /* two pages (E8C8)             */
    FI_MOVE,        /* arrow into folder (E8DE)     */
    FI_DELETE,      /* trash (E74D)                 */
    FI_RENAME,      /* pencil (E70F)                */
    FI_PROPERTIES,  /* info (E946)                  */
    FI_SHARE,       /* share (E72D)                 */
    FI_REFRESH,     /* circular arrow (E72C)        */
    FI_LIST,        /* list view (E8FD)             */
    FI_ICONS,       /* grid view (E80A)             */
    FI_CHECK,       /* checkmark (E73E)             */
    FI_X,           /* close X (E711)               */
    FI_BACK,        /* back arrow (E72B)            */
    FI_DEVICE,      /* phone/this device (E770? -> PDA shape) */
    FI_SDCARD,      /* SD card (a solid MDL2 has none; custom) */
    FI_COUNT
};

/* name -> id (0 when unknown) */
int fi_find(const char *name);

/* Draw icon `id` centered in the size x size box at (x, y). */
void fi_draw(IconFillFn fill, void *dc, int x, int y, int size,
             unsigned color, int id);

/* Amber two-tone folder (W10M Folder_Large.png colors). */
void fi_folder(IconFillFn fill, void *dc, int x, int y, int size,
               unsigned body, unsigned shade);

/* Shared stroke helpers (same contract as edge icons.c). */
void fi_line(IconFillFn fill, void *dc, int x0, int y0, int x1, int y1,
             int thick, unsigned c);
void fi_rect(IconFillFn fill, void *dc, int x, int y, int w, int h,
             int thick, unsigned c);
void fi_box(IconFillFn fill, void *dc, int x, int y, int w, int h,
            unsigned c);
void fi_circle(IconFillFn fill, void *dc, int cx, int cy, int r,
               int thick, unsigned c);
void fi_dot(IconFillFn fill, void *dc, int cx, int cy, int r, unsigned c);

#endif
