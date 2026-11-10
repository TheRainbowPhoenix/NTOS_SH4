/* mdrender.c — "web engine" for text pages: a tiny Markdown + HTML4-subset
 * core for the Edge POC (the successor of the gint md_viewer.py mock, but
 * integer-only, no heap, single pass, and it feeds find-in-page).
 *
 * Pipeline: source text -> MdLine display list (runs with styles) ->
 * paint via the host renderer. Markdown and HTML share the same run
 * emitter: <b> and **, <a href> and [t](u), <h1> and #, <hr> and ---.
 * <script> bodies are forwarded to the CHAKRA.DLL stub (JsRunScript)
 * and never rendered — the POC's entire scripting story.
 *
 * Limits (4 MB device): 128 lines, 320 runs, 8 runs per wrapped line,
 * 4 cached images, 16 KB sources. All static arenas.
 */
#include "nt_app.h"
#include "uxaml.h"
#include "icons.h"
#include "font_bitmap.h"

/* ---- host hooks supplied by edge_app.c ---- */
int md_read_file(const char *name, unsigned char *buf, int cap); /* fls0 */
U32 md_rgb(int r, int g, int b);
void md_fill(void *dc, int l, int t, int r, int b, U32 c);
void md_text(void *dc, int x, int y, const char *s, int len, U32 c, int h, int bold);
int md_text_w(const char *s, int len, int h);
void md_svfdraw(void *dc, const unsigned char *data, unsigned len, int x, int y, int w, int h);
int md_chakra(const char *script); /* JsRunScript stub */

#define MD_MAX_LINES 128
#define MD_MAX_RUNS 320
#define MD_RUNS_PER_LINE 8
#define MD_MAX_SRC (16 * 1024)
#define MD_MAX_IMG 4

enum {
    MD_LINK = 1, /* blue + underline */
    MD_CODE = 2, /* inline code tint */
    MD_BOLD = 4,
};
typedef struct {
    const char *s; /* into the source buffer */
    short len;
    unsigned char style;
} MdRun;
typedef struct {
    MdRun run[MD_RUNS_PER_LINE];
    unsigned char nrun;
    unsigned char kind; /* 0 text, 1..4 heading, 5 code-block, 6 quote, 7 list-bullet */
    unsigned char h;    /* font height */
} MdLine;

typedef struct {
    unsigned char data[2048];
    unsigned len;
    char name[24];
} MdImg;

static unsigned char md_src[MD_MAX_SRC];
static int md_src_len;
static MdLine md_lines[MD_MAX_LINES];
static int md_line_count;
static char md_loaded_name[24];
static MdImg md_imgs[MD_MAX_IMG];
static int md_img_count;

/* ---- tiny helpers (no CRT) ---- */
static int mdp_len(const char *s)
{
    int n = 0;
    while (s[n])
        ++n;
    return n;
}
static int mdp_eq(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static int mdp_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}
static int mdp_starts(const char *p, const char *pre, int plen)
{
    for (int i = 0; i < plen; ++i)
        if (p[i] != pre[i])
            return 0;
    return 1;
}
static int mdp_lt(const char *p, const char *tag) /* "<tag " or "<tag>" ci */
{
    if (*p != '<')
        return 0;
    ++p;
    for (int i = 0; tag[i]; ++i)
        if (mdp_lower((unsigned char)p[i]) != tag[i])
            return 0;
    char c = p[mdp_len(tag)];
    return c == '>' || c == ' ' || c == '/' || c == '\t';
}
static const char *mdp_attr(const char *tag, const char *name, int *outlen)
{ /* inside "<img src=\"...\">": find name=" value */
    for (const char *p = tag; *p && *p != '>'; ++p) {
        if (mdp_starts(p, name, mdp_len(name)) && (p[mdp_len(name)] == '=' ||
            (p[mdp_len(name)] == ' ' && p[mdp_len(name) + 1] == '='))) {
            p += mdp_len(name);
            while (*p == ' ' || *p == '=')
                ++p;
            if (*p == '"' || *p == '\'')
                ++p;
            const char *v = p;
            while (*p && *p != '"' && *p != '\'' && *p != '>' && *p != ' ')
                ++p;
            *outlen = (int)(p - v);
            return v;
        }
    }
    *outlen = 0;
    return 0;
}

/* ---- inline scanner: emits runs for one source line ---- */
typedef struct {
    MdLine *out;
    int emitted;
} MdCtx;

static void emit_run(MdCtx *c, const char *s, int len, int style)
{
    if (len <= 0 || c->out->nrun >= MD_RUNS_PER_LINE)
        return;
    MdRun *r = &c->out->run[c->out->nrun++];
    r->s = s;
    r->len = (short)len;
    r->style = (unsigned char)style;
    c->emitted = 1;
}

/* split one source line into runs; handles md inline + html inline tags.
 * Returns the position after the tag that closed this nesting level, so a
 * parent resumes right behind its child's </b>/</a>/</code>. */
static char *skip_past_tag(char *p, char *end, const char *tag, int slash)
{
    /* positioned at '<'; step over <[/]tag ... > */
    int tl = mdp_len(tag);
    p += 1 + (slash ? 1 : 0) + tl;
    while (p < end && *p != '>')
        ++p;
    if (p < end)
        ++p;
    return p;
}
static char *skip_to_close(char *p, char *end, const char *tag)
{
    while (p < end && *p != '>')
        ++p;
    if (p < end)
        ++p;
    return p;
}

static char *scan_inline(MdCtx *c, char *p, char *end, int style)
{
    while (p < end) {
        if (*p == '<') {
            if (mdp_lt(p, "b")) {
                p = skip_to_close(p, end, 0);
                p = scan_inline(c, p, end, style | MD_BOLD);
                continue; /* child consumed through </b> */
            }
            if (mdp_lt(p, "/b") || mdp_lt(p, "/i") || mdp_lt(p, "/u") ||
                mdp_lt(p, "/em") || mdp_lt(p, "/strong") || mdp_lt(p, "/a") ||
                mdp_lt(p, "/code")) {
                while (p < end && *p != '>')
                    ++p;
                if (p < end)
                    ++p;
                return p; /* hand the rest back to the parent */
            }
            if (mdp_lt(p, "code")) {
                p = skip_to_close(p, end, 0);
                p = scan_inline(c, p, end, style | MD_CODE);
                continue;
            }
            if (mdp_lt(p, "a")) {
                p = skip_to_close(p, end, 0);
                p = scan_inline(c, p, end, style | MD_LINK);
                continue;
            }
            if (mdp_lt(p, "i") || mdp_lt(p, "u") || mdp_lt(p, "strong") ||
                mdp_lt(p, "em")) {
                p = skip_to_close(p, end, 0);
                p = scan_inline(c, p, end, style | MD_BOLD);
                continue;
            }
            if (mdp_lt(p, "br")) { /* line break inside a paragraph: soft gap */
                p = skip_to_close(p, end, 0);
                continue;
            }
            /* unknown tag: skip it entirely */
            while (p < end && *p != '>')
                ++p;
            if (p < end)
                ++p;
            continue;
        }
        /* markdown inline */
        if (p + 1 < end && p[0] == '*' && p[1] == '*') {
            char *q = p + 2;
            while (q + 1 < end && !(q[0] == '*' && q[1] == '*'))
                ++q;
            if (q + 1 < end) {
                emit_run(c, p + 2, (int)(q - p - 2), style | MD_BOLD);
                p = q + 2;
                continue;
            }
        }
        if (p + 1 < end && p[0] == '`') {
            char *q = p + 1;
            while (q < end && *q != '`')
                ++q;
            if (q < end) {
                emit_run(c, p + 1, (int)(q - p - 1), style | MD_CODE);
                p = q + 1;
                continue;
            }
        }
        if (p + 1 < end && p[0] == '[') {
            char *q = p + 1;
            while (q < end && *q != ']')
                ++q;
            if (q + 1 < end && q[1] == '(') {
                char *r = q + 2;
                while (r < end && *r != ')')
                    ++r;
                if (r < end) {
                    emit_run(c, p + 1, (int)(q - p - 1), style | MD_LINK);
                    p = r + 1;
                    continue;
                }
            }
        }
        char *q = p + 1;
        while (q < end && *q != '<' && *q != '*' && *q != '`' && *q != '[')
            ++q;
        emit_run(c, p, (int)(q - p), style);
        p = q;
    }
    return p;
}

/* ---- block parser: source -> MdLine list ---- */
static void md_new_line(int kind, int h)
{
    if (md_line_count >= MD_MAX_LINES)
        return;
    MdLine *l = &md_lines[md_line_count++];
    l->nrun = 0;
    l->kind = (unsigned char)kind;
    l->h = (unsigned char)h;
}

static const char *skip_script(const char *p)
{ /* forward the body to the chakra stub; never rendered */
    static char script_buf[512];
    const char *close = p;
    while (*close) {
        if (close[0] == '<' && mdp_lower((unsigned char)close[1]) == '/' &&
            mdp_lower((unsigned char)close[2]) == 's')
            break;
        ++close;
    }
    int n = (int)(close - p);
    if (n > 500)
        n = 500;
    for (int i = 0; i < n; ++i)
        script_buf[i] = p[i];
    script_buf[n] = 0;
    md_chakra(script_buf);
    while (*close && *close != '>')
        ++close;
    return *close ? close + 1 : close;
}

static void md_parse(void)
{
    md_line_count = 0;
    char *p = (char *)md_src;
    char *end = (char *)md_src + md_src_len;
    int in_code = 0;
    while (p < end) {
        char *nl = p;
        while (nl < end && *nl != '\n')
            ++nl;
        int len = (int)(nl - p);
        if (len > 0 && p[len - 1] == '\r')
            --len;
        /* block-level html + md */
        if (mdp_lt(p, "script")) {
            const char *after = skip_script(p);
            p = (char *)after;
            if (p >= nl && p > nl)
                continue;
            p = (char *)after;
            continue;
        }
        if (in_code) {
            if (len >= 3 && p[0] == '`' && p[1] == '`' && p[2] == '`')
                in_code = 0;
            else {
                md_new_line(5, 12);
                MdCtx c = {&md_lines[md_line_count - 1], 0};
                emit_run(&c, p, len, MD_CODE);
            }
            p = nl + 1;
            continue;
        }
        if (len >= 3 && p[0] == '`' && p[1] == '`' && p[2] == '`') {
            in_code = 1;
            p = nl + 1;
            continue;
        }
        if (mdp_lt(p, "h1")) { md_new_line(1, 22); goto htext; }
        if (mdp_lt(p, "h2")) { md_new_line(2, 18); goto htext; }
        if (mdp_lt(p, "h3") || mdp_lt(p, "h4")) { md_new_line(3, 16); goto htext; }
        if (mdp_lt(p, "p") || mdp_lt(p, "li") || mdp_lt(p, "ul") ||
            mdp_lt(p, "ol")) {
            while (p < nl && *p != '>')
                ++p;
            if (p < nl)
                ++p;
            while (p < nl && (*p == ' ' || *p == '\t'))
                ++p;
            md_new_line(0, 12);
            { MdCtx c = {&md_lines[md_line_count - 1], 0};
              scan_inline(&c, p, nl, 0); }
            p = nl + 1;
            continue;
        }
        if (mdp_lt(p, "blockquote")) {
            while (p < nl && *p != '>')
                ++p;
            if (p < nl)
                ++p;
            md_new_line(6, 12);
            { MdCtx c = {&md_lines[md_line_count - 1], 0};
              scan_inline(&c, p, nl, 0); }
            p = nl + 1;
            continue;
        }
        if (mdp_lt(p, "hr") || (len >= 3 && (p[0] == '-' || p[0] == '*') &&
                                p[1] == p[0] && p[2] == p[0] && p[3] == p[0])) {
            md_new_line(8, 2); /* hr rule */
            p = nl + 1;
            continue;
        }
        if (mdp_lt(p, "img")) {
            int wl = 0; /* only src matters; fixed 48px art slot */
            const char *src = mdp_attr(p, "src", &wl);
            md_new_line(9, 48); /* image line */
            MdLine *l = &md_lines[md_line_count - 1];
            if (l->nrun < MD_RUNS_PER_LINE) {
                l->run[l->nrun].s = src;
                l->run[l->nrun].len = (short)wl;
                l->run[l->nrun].style = 9; /* image marker */
                ++l->nrun;
            }
            p = nl + 1;
            continue;
        }
        if (len >= 1 && p[0] == '#') {
            int level = 0;
            while (level < len && p[level] == '#')
                ++level;
            int skip = level < len && p[level] == ' ' ? level + 1 : level;
            md_new_line(level <= 1 ? 1 : level == 2 ? 2 : 3,
                        level <= 1 ? 22 : level == 2 ? 18 : 16);
            { MdCtx c = {&md_lines[md_line_count - 1], 0};
              scan_inline(&c, p + skip, nl, MD_BOLD); }
            p = nl + 1;
            continue;
        }
        if (len >= 2 && (p[0] == '-' || p[0] == '*' || p[0] == '+') && p[1] == ' ') {
            md_new_line(7, 12);
            { MdCtx c = {&md_lines[md_line_count - 1], 0};
              scan_inline(&c, p + 2, nl, 0); }
            p = nl + 1;
            continue;
        }
        if (len >= 3 && p[0] >= '0' && p[0] <= '9' && p[1] == '.' && p[2] == ' ') {
            md_new_line(10, 12); /* numbered list: the "N." stays in the text */
            { MdCtx c = {&md_lines[md_line_count - 1], 0};
              scan_inline(&c, p, nl, 0); }
            p = nl + 1;
            continue;
        }
        if (len >= 1 && p[0] == '>') {
            int skip = len > 1 && p[1] == ' ' ? 2 : 1;
            md_new_line(6, 12);
            { MdCtx c = {&md_lines[md_line_count - 1], 0};
              scan_inline(&c, p + skip, nl, 0); }
            p = nl + 1;
            continue;
        }
        if (len == 0) {
            p = nl + 1;
            continue; /* blank line: just gap */
        }
        /* paragraph text */
        md_new_line(0, 12);
        { MdCtx c = {&md_lines[md_line_count - 1], 0};
          scan_inline(&c, p, nl, 0); }
        p = nl + 1;
        continue;
    htext:
        while (p < nl && *p != '>')
            ++p;
        if (p < nl)
            ++p;
        while (p < nl && (*p == ' ' || *p == '\t'))
            ++p;
        { MdCtx c = {&md_lines[md_line_count - 1], 0};
          scan_inline(&c, p, nl, MD_BOLD); }
        p = nl + 1;
    }
}

/* ---- word wrap: source lines -> visual lines (shared by paint+find) ----
 * Words keep their leading space inside the run, so painters advance
 * naturally and find concatenation still sees real spaces. Continuation
 * lines carry kind|0x80: same indent/quote bar, no bullet dot, no number. */
#define MD_MAX_VLINES 192
static MdLine md_vlines[MD_MAX_VLINES];
static int md_vline_count;

static void md_wrap(void)
{
    md_vline_count = 0;
    for (int li = 0; li < md_line_count; ++li) {
        MdLine *sl = &md_lines[li];
        if (sl->kind == 8 || sl->kind == 9) { /* hr / image pass through */
            if (md_vline_count < MD_MAX_VLINES)
                md_vlines[md_vline_count++] = *sl;
            continue;
        }
        if (sl->nrun == 0) { /* spacer keeps gaps between blocks */
            if (md_vline_count < MD_MAX_VLINES)
                md_vlines[md_vline_count++] = *sl;
            continue;
        }
        int k = sl->kind & 0x0F;
        int x0 = k == 10 ? 26 : k == 7 ? 34 : k == 6 ? 30 : k == 5 ? 24 : 16;
        int maxw = 304 - x0;
        int lh = sl->h < 12 ? 12 : sl->h;
        int cur = -1, used = 0, cont = 0;
        for (int r = 0; r < sl->nrun; ++r) {
            const char *rs = sl->run[r].s;
            int rlen = sl->run[r].len, i = 0;
            while (i < rlen) {
                while (i < rlen && rs[i] == ' ')
                    ++i;
                int w0 = i;
                while (i < rlen && rs[i] != ' ')
                    ++i;
                if (i == w0)
                    break;
                int wlen = i - w0;
                /* attach the leading space to every word but line starters */
                int lead = 0;
                if (used) {
                    w0 -= 1;
                    wlen += 1;
                    lead = 1;
                }
                int ww = md_text_w(rs + w0 + (lead ? 0 : 0), wlen, lh);
                (void)lead;
                if (used && used + ww > maxw) { /* wrap: demote to continuation */
                    cur = -1;
                    used = 0;
                    cont = 1;
                    w0 += 1;
                    wlen -= 1;
                    ww = md_text_w(rs + w0, wlen, lh);
                }
                if (cur < 0) {
                    if (md_vline_count >= MD_MAX_VLINES)
                        return;
                    cur = md_vline_count++;
                    md_vlines[cur].nrun = 0;
                    md_vlines[cur].h = sl->h;
                    md_vlines[cur].kind = (unsigned char)(sl->kind | (cont ? 0x80 : 0));
                }
                MdLine *vl = &md_vlines[cur];
                if (vl->nrun < MD_RUNS_PER_LINE) {
                    vl->run[vl->nrun].s = rs + w0;
                    vl->run[vl->nrun].len = (short)wlen;
                    vl->run[vl->nrun].style = sl->run[r].style;
                    ++vl->nrun;
                }
                used += ww;
            }
        }
    }
}

/* ---- image cache ---- *//* ---- image cache ---- */
static const MdImg *md_img_get(const char *name, int len)
{
    for (int i = 0; i < md_img_count; ++i)
        if (mdp_len(md_imgs[i].name) == len && mdp_eq(md_imgs[i].name, name, len))
            return &md_imgs[i];
    if (md_img_count >= MD_MAX_IMG || len >= 22)
        return 0;
    MdImg *im = &md_imgs[md_img_count++];
    for (int i = 0; i < len; ++i)
        im->name[i] = name[i];
    im->name[len] = 0;
    im->len = (unsigned)md_read_file(im->name, im->data, sizeof(im->data));
    return im->len ? im : 0;
}

/* ---- public API ---- */
int md_page_load(const char *name)
{ /* name without directory; returns line count or 0 */
    if (mdp_len(name) >= sizeof(md_loaded_name))
        return 0;
    if (mdp_eq(md_loaded_name, name, mdp_len(name) + 1) && md_line_count)
        return md_line_count; /* cached */
    md_src_len = md_read_file(name, md_src, sizeof(md_src) - 1);
    if (md_src_len <= 0)
        return 0;
    md_src[md_src_len] = 0;
    for (int i = 0; i <= mdp_len(name); ++i)
        md_loaded_name[i] = name[i];
    md_img_count = 0;
    md_parse();
    md_wrap();
    return md_line_count;
}

int md_line_count_get(void)
{
    return md_line_count;
}

/* find-in-page: case-insensitive substring across the visual lines;
 * fills match rects (page coords). Returns the match count. */
int md_find(const char *query, int *out_x, int *out_y, int *out_w, int cap,
            void *unused)
{
    (void)unused;
    int ql = mdp_len(query), n = 0;
    if (!ql)
        return 0;
    int y = 8;
    for (int li = 0; li < md_vline_count && n < cap; ++li) {
        MdLine *l = &md_vlines[li];
        int k = l->kind & 0x0F;
        if (k >= 1 && k <= 4)
            y += 10; /* heading gap */
        int lh = l->h < 12 ? 12 : l->h;
        if (k == 8) {
            y += 16;
            continue;
        }
        if (k == 9) {
            y += 60;
            continue;
        }
        char buf[96];
        int bl = 0;
        for (int r = 0; r < l->nrun && bl < 90; ++r)
            for (int q = 0; q < l->run[r].len && bl < 90; ++q)
                buf[bl++] = l->run[r].s[q];
        buf[bl] = 0;
        int x = k == 7 ? 34 : k == 10 ? 26 : k == 6 ? 30 : k == 5 ? 24 : 16;
        for (int i = 0; i + ql <= bl && n < cap; ++i) {
            int hit = 1;
            for (int q = 0; q < ql; ++q)
                if (mdp_lower((unsigned char)buf[i + q]) != mdp_lower((unsigned char)query[q])) {
                    hit = 0;
                    break;
                }
            if (hit) {
                out_x[n] = x + md_text_w(buf, i, lh);
                out_y[n] = y;
                out_w[n] = md_text_w(buf + i, ql, lh);
                ++n;
                i += ql - 1;
            }
        }
        y += k == 1 ? 30 : k == 2 ? 24 : 17;
    }
    return n;
}

/* paint the page into 0..480; returns the used height */
int md_paint(void *dc)
{
    md_fill(dc, 0, 0, 320, 480, md_rgb(0xFF, 0xFF, 0xFF));
    int y = 8;
    for (int li = 0; li < md_vline_count; ++li) {
        MdLine *l = &md_vlines[li];
        int k = l->kind & 0x0F;
        int cont = l->kind & 0x80;
        if (k >= 1 && k <= 4) { /* heading */
            y += 10;
            int x = 16;
            for (int r = 0; r < l->nrun; ++r) {
                md_text(dc, x, y, l->run[r].s, l->run[r].len,
                        md_rgb(0x00, 0x00, 0x00), l->h, 1);
                x += md_text_w(l->run[r].s, l->run[r].len, l->h);
            }
            y += l->h + 6;
            continue;
        }
        if (k == 8) { /* hr */
            y += 8;
            md_fill(dc, 16, y, 304, y + 2, md_rgb(0xA2, 0xA9, 0xB1));
            y += 10;
            continue;
        }
        if (k == 9) { /* image line */
            y += 8;
            const MdImg *im = 0;
            for (int r = 0; r < l->nrun; ++r)
                if (l->run[r].style == 9 && l->run[r].s) {
                    im = md_img_get(l->run[r].s, l->run[r].len);
                    break;
                }
            if (im)
                md_svfdraw(dc, im->data, im->len, 136, y, 48, 48);
            else
                md_fill(dc, 136, y, 184, y + 48, md_rgb(0x76, 0x76, 0x76)); /* load failed */
            md_fill(dc, 134, y, 186, y + 1, md_rgb(0x76, 0x76, 0x76));
            md_fill(dc, 134, y + 49, 186, y + 50, md_rgb(0x76, 0x76, 0x76));
            md_fill(dc, 134, y, 135, y + 50, md_rgb(0x76, 0x76, 0x76));
            md_fill(dc, 185, y, 186, y + 50, md_rgb(0x76, 0x76, 0x76));
            y += 52;
            continue;
        }
        int lh = l->h < 12 ? 12 : l->h;
        int x = k == 7 ? 34 : k == 10 ? 26 : k == 6 ? 30 : k == 5 ? 24 : 16;
        if (k == 7 && !cont) /* list bullet: first line only */
            md_fill(dc, 22, y + lh / 2 - 1, 27, y + lh / 2 + 2,
                    md_rgb(0x00, 0x00, 0x00));
        if (k == 5) /* code-block backdrop fuses across lines */
            md_fill(dc, 20, y - 4, 304, y + lh + 5, md_rgb(0xEF, 0xEF, 0xEF));
        if (k == 6) /* quote bar */
            md_fill(dc, 16, y - 2, 19, y + lh + 2, md_rgb(0x76, 0x76, 0x76));
        for (int r = 0; r < l->nrun; ++r) {
            MdRun *run = &l->run[r];
            U32 col = md_rgb(0x00, 0x00, 0x00);
            int bold = 0;
            if (run->style & MD_CODE)
                col = md_rgb(0x54, 0x59, 0x5D);
            if (run->style & MD_LINK)
                col = md_rgb(0x33, 0x66, 0xCC);
            if (run->style & MD_BOLD)
                bold = 1;
            if (k == 6)
                col = md_rgb(0x54, 0x59, 0x5D);
            if (run->len > 0) {
                if ((run->style & MD_CODE) && k != 5) /* inline code chip */
                    md_fill(dc, x - 2, y - 2, x + md_text_w(run->s, run->len, lh) + 2,
                            y + lh + 2, md_rgb(0xEF, 0xEF, 0xEF));
                md_text(dc, x, y, run->s, run->len, col, lh, bold);
            }
            if ((run->style & MD_LINK) && run->len > 0)
                md_fill(dc, x, y + lh - 1,
                        x + md_text_w(run->s, run->len, lh), y + lh,
                        md_rgb(0x33, 0x66, 0xCC));
            x += md_text_w(run->s, run->len, lh);
        }
        y += k == 5 ? lh + 7 : lh + 5;
    }
    return y;
}
