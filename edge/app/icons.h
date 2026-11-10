/* icons.h — MDL2-style outline icon set for the Edge chrome (integer only).
 *
 * Real Edge draws its chrome icons from BrowserMDL2.ttf / ReadMDL2.ttf
 * (Segoe MDL2 Assets). We cannot ship a TTF rasterizer on the device, so
 * the POC redraws the same glyphs as tiny vector strokes: lines, circles
 * and polygons over the GDI fill backend, no floats, no tables bigger
 * than the 10-vertex star.
 */
#ifndef ICONS_H
#define ICONS_H

typedef void (*IconFillFn)(void *dc, int l, int t, int r, int b, unsigned color);

enum {
    IC_NONE = 0,
    IC_TABS,       /* two overlapping windows        */
    IC_RELOAD,     /* circular arrow                 */
    IC_LOCK,       /* padlock (https)                */
    IC_READER,     /* open book (reading view)       */
    IC_MORE,       /* "..." ellipsis                 */
    IC_STAR,       /* favorites                      */
    IC_BOOK,       /* reading list (closed book)     */
    IC_CLOCK,      /* history                        */
    IC_DOWNLOAD,   /* arrow into tray                */
    IC_PLUS,       /* new tab                        */
    IC_INPRIVATE,  /* tab + slash                    */
    IC_CLOSE,      /* X                              */
    IC_SHARE,      /* arrow out of box               */
    IC_FIND,       /* magnifier                      */
    IC_PIN,        /* map pin                        */
    IC_FORWARD,    /* arrow right                    */
    IC_BACK,       /* arrow left                     */
    IC_CHECK,      /* checkmark                      */
    IC_CHEVRON,    /* ">"                            */
    IC_CHEVL,      /* "<"                            */
    IC_SETTINGS,   /* gear                           */
    IC_READLIST,   /* reading list: stacked lines+hook */
    IC_COUNT
};

/* name -> id (0 when unknown); names match Segoe MDL2 usage in markup */
int ic_find(const char *name);

/* Draw icon `id` centered in the size x size box at (x, y). */
void ic_draw(IconFillFn fill, void *dc, int x, int y, int size,
             unsigned color, int id);

/* Shared stroke helpers (also used by the app for custom chrome). */
void ic_line(IconFillFn fill, void *dc, int x0, int y0, int x1, int y1,
             int thick, unsigned c);
void ic_rect(IconFillFn fill, void *dc, int x, int y, int w, int h,
             int thick, unsigned c);
void ic_circle(IconFillFn fill, void *dc, int cx, int cy, int r,
               int thick, unsigned c);
void ic_dot(IconFillFn fill, void *dc, int cx, int cy, int r, unsigned c);

#endif
