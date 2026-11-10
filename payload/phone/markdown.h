/* markdown.h — MARKDOWN.DLL: the shared web-document engine for NTOS.
 *
 * A tiny Markdown + HTML4 + CSS1-subset renderer in the Wine spirit:
 * no ported library, integer-only, static arenas, one pass per phase.
 * Any app can host it (Edge, FileExplorer, future readers) by filling
 * an MdHost callback table — the DLL imports nothing itself.
 *
 *   md_set_host(&host)
 *   md_load(MD_SRC_MD, src, len)     -> parse (+CSS) into the doc arena
 *   md_layout(320)                   -> box model + floats -> paint ops
 *   md_paint(dc, sx, sy, 320, 480)   -> draw the viewport window
 *   md_find("exam", ...)             -> find-in-page over the ops
 *   md_link_at(x, y)                 -> hit-test blue links
 *   md_reader(1) + md_layout()       -> reader mode (styles stripped)
 *
 * Phases are separate so reader mode and re-wrap are a re-layout, not
 * a re-parse, and find never touches the source again.
 */
#ifndef MARKDOWN_H
#define MARKDOWN_H

/* export the API when built as the host PE module (device build uses the
 * packer's named exports instead) */
#if defined(NTOS_LINUX_BUILD) && !defined(MD_STATIC)
#define MD_API __attribute__((dllexport))
#else
#define MD_API
#endif

/* host hooks the engine calls; all length-based, no CRT */
typedef struct MdHost {
    void *dc; /* opaque drawing context handed back to the painters */
    void (*fill)(void *dc, int l, int t, int r, int b, unsigned c);
    /* flags: 1 bold, 2 italic, 4 underline, 8 strike-through */
    void (*text)(void *dc, int x, int y, const char *s, int len,
                 unsigned c, int h, int flags);
    int (*text_w)(const char *s, int len, int h);
    void (*svf)(void *dc, const unsigned char *data, unsigned len,
                int x, int y, int w, int h); /* .svf vector art */
    void (*ellipse)(void *dc, int l, int t, int r, int b, unsigned c,
                    int filled); /* form radio dots */
    /* storage / system */
    int (*read_file)(const char *name, unsigned char *buf, int cap);
    int (*chakra)(const char *script); /* <script> -> CHAKRA.DLL stub */
} MdHost;

/* source dialects */
#define MD_SRC_MD 0    /* markdown (frontmatter tolerated, mdx too) */
#define MD_SRC_HTML 1  /* html4 subset: tags, attrs, <style> css1 */
#define MD_SRC_TEXT 2  /* plain text: paragraphs, long urls kept */

/* reader mode targets */
#define MD_READER_OFF 0
#define MD_READER_ON 1

/* text style flags shared with the host text hook */
#define MDTF_BOLD 1
#define MDTF_ITAL 2
#define MDTF_UL 4
#define MDTF_STRIKE 8

/* public API (named exports; host apps import with the md_ prefix) */
void MD_API md_set_host(const MdHost *host);
int MD_API md_load(int dialect, const char *src, int len); /* 0 = too big/broken */
void MD_API md_layout(int view_w); /* (re)flow; also used by reader toggling */
void MD_API md_reader(int on);
int MD_API md_reader_is(void);
void MD_API md_paint(const MdHost *h, int sx, int sy, int vw, int vh);
void MD_API md_size(int *w, int *h); /* laid-out page extent */
const MD_API char *md_title(int *len); /* <title> / first h1 / frontmatter */
int MD_API md_find(const char *query, int *x, int *y, int *w, int cap);
/* returns 1 + fills href when (x,y) page coords hit a laid-out link */
int MD_API md_link_at(int x, int y, const char **href, int *len);
int MD_API md_anchor_scroll(int y); /* clamp a scroll target, returns it */

#endif /* MARKDOWN_H */
