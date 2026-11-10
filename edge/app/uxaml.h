/* uxaml.h — uXAML: tiny binary XAML subset for NTOS (Edge POC UI).
 *
 * Design goals (100 MHz SH4, 4 MB RAM, ~1 s per file read):
 *   - zero XML parsing at runtime: pages ship precompiled
 *   - single-pass, forward-only loader: no random access, no second read
 *   - every key predefined: element kinds and attributes are u8 enums, so a
 *     page "upper-converts" to XML losslessly (uxaml_gen.py does both ways)
 *   - no heap needed by the loader: nodes live in a static arena and the
 *     loader only stores u16 indices, not pointers
 *
 * File layout (little endian):
 *   offset 0:  "UXA1"                  magic
 *   +4:        u16 version (=1)   u16 flags (=0)
 *   +8:        u16 doc_count       u16 string_count
 *   +12:       u32 node_bytes      (node stream size)
 *   +16:       u16 doc[]           doc_count entries:
 *                u16 name_str  u32 node_offset  u16 node_count
 *   +D:        string heap: string_count entries { u8 len, bytes[len] }
 *              (len 255 = escape: extra u16 real length)
 *   +S:        node stream (docs in order, parents before children)
 *
 * Node stream entry:
 *   u8 kind          UX_* element id
 *   u8 attr_count
 *   attr_count x { u8 attr (UXA_*), u8 type, payload }
 *      type 0 STR: u16 string-heap index
 *      type 1 INT: i32
 *      type 2 CLR: u32 0x00BBGGRR (device RGB_ order)
 *      type 3 BOOL:u8
 *   u8 child_count   (child subtrees follow immediately, in order)
 */
#ifndef UXAML_H
#define UXAML_H

#include <stdint.h>

#define UXAML_MAGIC 0x31415855u /* "UXA1" little endian */
#define UXAML_VERSION 1

/* ---- element kinds ---- */
enum {
    UX_PAGE = 0,     /* doc root; X/Y/W/H = viewport override */
    UX_CANVAS,       /* absolute container */
    UX_STACKV,       /* vertical stack; children auto-Y */
    UX_STACKH,       /* horizontal stack; children auto-X */
    UX_TEXT,         /* TextBlock */
    UX_BUTTON,       /* tappable (Text/Glyph + Command) */
    UX_RECT,         /* filled rect with corner radius */
    UX_ELLIPSE,
    UX_GLYPH,        /* icon glyph (Text = ASCII art or symbol name) */
    UX_LIST,         /* scrollable list; children = rows (auto height) */
    UX_ROW,          /* list row (auto Y from parent, fixed height) */
    UX_TEXTBOX,      /* text input: Text/Placeholder/Caret/Active */
    UX_PROGRESS,     /* progress bar: Value/Max/Fill/Track */
    UX_CHECK,        /* checkbox: Checked/Text (shared uixctl painter) */
    UX_SCROLL,       /* scroll container for one child */
    UX_TOGGLE,       /* toggle switch: Checked (shared uixctl painter) */
    UX_KIND_COUNT
};

/* ---- attributes ---- */
enum {
    UXA_X = 0,
    UXA_Y,
    UXA_W,
    UXA_H,
    UXA_TEXT,
    UXA_COLOR,      /* foreground */
    UXA_BG,         /* background */
    UXA_SIZE,       /* font height px */
    UXA_BOLD,
    UXA_RADIUS,     /* corner radius px */
    UXA_NAME,       /* automation name (Edge UI map keys) */
    UXA_COMMAND,    /* command id string */
    UXA_SRC,        /* asset source (unused v1: reserved) */
    UXA_VALUE,
    UXA_MAX,
    UXA_CHECKED,
    UXA_VISIBLE,
    UXA_ALIGN,      /* 0 left 1 center 2 right (text) */
    UXA_BORDER,     /* Rect: 1px outline in Color instead of Bg fill */
    UXA_PLACEHOLDER,/* TextBox hint text (shown when empty) */
    UXA_COUNT
};

/* ---- parsed node (static arena, no pointers but child indices) ---- */
#define UX_MAX_NODES 384
#define UX_MAX_ATTRS 10
#define UX_MAX_DOCS 24
#define UX_MAX_STRINGS 448
#define UX_STR_MAX 96

typedef struct {
    uint8_t kind;
    uint8_t attr_count;
    uint8_t attr[UX_MAX_ATTRS];       /* UXA_* ids */
    uint8_t type[UX_MAX_ATTRS];       /* 0 str 1 int 2 clr 3 bool */
    int32_t num[UX_MAX_ATTRS];        /* int value / color / bool / str idx */
    uint8_t child_count;
    uint16_t child[16];               /* node indices */
    uint16_t parent;                  /* UX_NONE for root */
} UxNode;

#define UX_NONE 0xFFFFu

typedef struct {
    char name[UX_STR_MAX];
    uint32_t node_offset;
    uint16_t node_count;
    uint16_t root;                    /* resolved node index */
} UxDoc;

typedef struct {
    uint16_t doc_count, string_count;
    uint32_t node_bytes;
    UxDoc docs[UX_MAX_DOCS];
    char strings[UX_MAX_STRINGS][UX_STR_MAX];
    uint16_t node_count;
    UxNode nodes[UX_MAX_NODES];
} UxFile;

/* Parse a whole .uxaml blob (already read in the single device file read).
 * Returns 0 on success; on failure *err gets a short reason string. */
int ux_parse(const uint8_t *data, uint32_t size, UxFile *out, const char **err);

/* Attribute lookup: returns index in n->attr[] or -1. */
int ux_attr(const UxNode *n, uint8_t attr_id);

/* Convenience getters with defaults. */
int32_t ux_int(const UxFile *f, const UxNode *n, uint8_t attr_id, int32_t def);
const char *ux_str(const UxFile *f, const UxNode *n, uint8_t attr_id, const char *def);
uint32_t ux_clr(const UxFile *f, const UxNode *n, uint8_t attr_id, uint32_t def);

/* Find doc by name; returns NULL if absent. */
const UxDoc *ux_find_doc(const UxFile *f, const char *name);

/* Find first node by Name attribute (BFS from root). */
const UxNode *ux_find_name(const UxFile *f, const UxDoc *d, const char *name);

#endif
