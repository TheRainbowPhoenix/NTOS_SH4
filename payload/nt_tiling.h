/* NTOS split-layout geometry; freestanding, integer-only, no framebuffer allocation.
 * The layout belongs to the USER/GWES layer, not the display HAL.
 * Rectangle edges are exclusive (Win32 RECT convention). */
#ifndef NTOS_TILING_H
#define NTOS_TILING_H

typedef struct {
    int left, top, right, bottom;
} NT_TILE_RECT;

typedef struct {
    int width, height, top, bottom;
    unsigned split, sideways, swapped, primary;
} NT_TILE_STATE;

static inline void nt_tile_init(NT_TILE_STATE *s, int width, int height, int top)
{
    s->width = width;
    s->height = height;
    s->top = top;
    s->bottom = height;
    s->split = 0;
    s->sideways = 0;
    s->swapped = 0;
    s->primary = 0;
}

/* Change available work area without resizing the physical display.
 * Used when an input panel occupies the bottom of the screen. */
static inline void nt_tile_work_bottom(NT_TILE_STATE *s, int bottom)
{
    if (bottom < s->top + 48)
        bottom = s->top + 48;
    if (bottom > s->height)
        bottom = s->height;
    s->bottom = bottom;
}

/* Returns zero for a hidden pane. Full screen uses the active pane alone. */
static inline int nt_tile_rect(const NT_TILE_STATE *s, unsigned pane, NT_TILE_RECT *r)
{
    int mid;
    unsigned position;
    if (!r || !s || pane > 1 || s->width < 2 || s->bottom <= s->top)
        return 0;
    if (!s->split && pane != s->primary)
        return 0;
    r->left = 0;
    r->top = s->top;
    r->right = s->width;
    r->bottom = s->bottom;
    if (!s->split)
        return 1;
    position = pane ^ s->swapped;
    if (s->sideways) {
        mid = s->width >> 1;
        if (position == 0)
            r->right = mid;
        else
            r->left = mid;
    } else {
        mid = s->top + ((s->bottom - s->top) >> 1);
        if (position == 0)
            r->bottom = mid;
        else
            r->top = mid;
    }
    return r->right > r->left && r->bottom > r->top;
}

/* Every pane keeps a title/close handle even while split. */
static inline int nt_tile_client(const NT_TILE_STATE *s, unsigned pane, NT_TILE_RECT *r)
{
    if (!nt_tile_rect(s, pane, r))
        return 0;
    r->top += 22;
    return r->top < r->bottom;
}

static inline int nt_tile_hit(const NT_TILE_STATE *s, int x, int y)
{
    NT_TILE_RECT r;
    if (x < 0 || y < 0 || x >= s->width || y >= s->bottom)
        return -1;
    for (unsigned i = 0; i != 2; ++i)
        if (nt_tile_rect(s, i, &r) && x >= r.left && x < r.right && y >= r.top &&
            y < r.bottom)
            return (int)i;
    return -1;
}

static inline void nt_tile_split(NT_TILE_STATE *s)
{
    s->split = !s->split;
}

static inline void nt_tile_swap(NT_TILE_STATE *s)
{
    s->swapped = !s->swapped;
}

static inline void nt_tile_rotate(NT_TILE_STATE *s)
{
    s->sideways = !s->sideways;
}

static inline void nt_tile_maximize(NT_TILE_STATE *s, unsigned pane)
{
    if (pane < 2) {
        s->primary = pane;
        s->split = 0;
    }
}
#endif
