/* uxaml.c — uXAML loader (see uxaml.h for the format contract). */
#include "uxaml.h"

/* freestanding: no libc on the device or in the zig -nostdlib host build */
static void *xmemcpy(void *d, const void *s, unsigned n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    while (n--)
        *dd++ = *ss++;
    return d;
}

static int xstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static unsigned xstrlen_(const char *s)
{
    unsigned n = 0;
    while (s[n])
        ++n;
    return n;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int rd_string(const uint8_t *p, uint32_t size, char *out)
{
    uint32_t len = p[0];
    p += 1;
    if (len == 255) {
        if (size < 3)
            return -1;
        len = rd16(p);
        p += 2;
    }
    if (len >= UX_STR_MAX)
        len = UX_STR_MAX - 1;
    xmemcpy(out, p, len);
    out[len] = 0;
    return (int)(1 + (len == 255 ? 2 : 0) + len);
}

int ux_parse(const uint8_t *data, uint32_t size, UxFile *out, const char **err)
{
    static const char *bad = "bad magic";
    static const char *toobig = "too many entries";
    static const char *trunc = "truncated";
    static const char *deep = "too many attrs/children";
    { unsigned char *m = (unsigned char *)out; for (unsigned i = 0; i < sizeof(*out); ++i) m[i] = 0; };
    if (size < 16 || rd32(data) != UXAML_MAGIC) {
        *err = bad;
        return -1;
    }
    uint16_t version = rd16(data + 4);
    if (version != UXAML_VERSION) {
        *err = "version";
        return -1;
    }
    out->doc_count = rd16(data + 8);
    out->string_count = rd16(data + 10);
    out->node_bytes = rd32(data + 12);
    if (out->doc_count > UX_MAX_DOCS || out->string_count > UX_MAX_STRINGS) {
        *err = toobig;
        return -1;
    }
    /* doc table */
    uint32_t off = 16;
    for (uint16_t i = 0; i < out->doc_count; ++i) {
        if (off + 8 > size) {
            *err = trunc;
            return -1;
        }
        uint16_t name_idx = rd16(data + off);
        out->docs[i].node_offset = rd32(data + off + 2);
        out->docs[i].node_count = rd16(data + off + 6);
        out->docs[i].root = UX_NONE;
        out->docs[i].name[0] = 0;
        (void)name_idx; /* resolved after the string heap is read */
        off += 8;
    }
    /* string heap */
    for (uint16_t i = 0; i < out->string_count; ++i) {
        if (off >= size) {
            *err = trunc;
            return -1;
        }
        int n = rd_string(data + off, size - off, out->strings[i]);
        if (n < 0) {
            *err = trunc;
            return -1;
        }
        off += (uint32_t)n;
    }
    /* node stream */
    uint32_t end = off + out->node_bytes;
    if (end > size) {
        *err = trunc;
        return -1;
    }
    uint16_t stack[UX_MAX_NODES]; /* node indices awaiting children */
    int sp = 0;
    uint16_t remaining[UX_MAX_NODES]; /* children still expected per stack level */
    for (uint32_t d = 0; d < out->doc_count; ++d) {
        UxDoc *doc = &out->docs[d];
        uint32_t p = off + doc->node_offset;
        uint32_t pend = p;
        for (uint16_t c = 0; c < doc->node_count; ++c) {
            if (p + 2 > end) {
                *err = trunc;
                return -1;
            }
            if (out->node_count >= UX_MAX_NODES) {
                *err = toobig;
                return -1;
            }
            UxNode *n = &out->nodes[out->node_count];
            n->kind = data[p++];
            n->attr_count = data[p++];
            if (n->attr_count > UX_MAX_ATTRS) {
                *err = deep;
                return -1;
            }
            for (uint8_t a = 0; a < n->attr_count; ++a) {
                if (p + 2 > end) {
                    *err = trunc;
                    return -1;
                }
                uint8_t attr = data[p++];
                uint8_t type = data[p++];
                n->attr[a] = attr;
                n->type[a] = type;
                if (type == 0) {
                    if (p + 2 > end) {
                        *err = trunc;
                        return -1;
                    }
                    n->num[a] = rd16(data + p);
                    p += 2;
                } else if (type == 1) {
                    if (p + 4 > end) {
                        *err = trunc;
                        return -1;
                    }
                    n->num[a] = (int32_t)rd32(data + p);
                    p += 4;
                } else if (type == 2) {
                    if (p + 4 > end) {
                        *err = trunc;
                        return -1;
                    }
                    n->num[a] = (int32_t)rd32(data + p);
                    p += 4;
                } else if (type == 3) {
                    if (p + 1 > end) {
                        *err = trunc;
                        return -1;
                    }
                    n->num[a] = data[p++];
                } else {
                    *err = "value type";
                    return -1;
                }
            }
            if (p + 1 > end) {
                *err = trunc;
                return -1;
            }
            n->child_count = data[p++];
            if (n->child_count > 16) {
                *err = deep;
                return -1;
            }
            n->parent = UX_NONE;
            uint16_t me = out->node_count++;
            if (sp == 0) {
                if (doc->root != UX_NONE) {
                    *err = "multiple roots";
                    return -1;
                }
                doc->root = me;
            } else {
                UxNode *parent = &out->nodes[stack[sp - 1]];
                if (parent->child_count <= remaining[sp - 1]) {
                    *err = "child overflow";
                    return -1;
                }
                n->parent = stack[sp - 1];
                parent->child[remaining[sp - 1]++] = me;
                if (remaining[sp - 1] == parent->child_count)
                    --sp; /* this parent is complete */
            }
            if (n->child_count) {
                stack[sp] = me;
                remaining[sp] = 0;
                ++sp;
            }
            (void)pend;
        }
        if (sp != 0) {
            *err = "unbalanced tree";
            return -1;
        }
    }
    /* resolve doc names now that strings exist */
    uint32_t doff = 16;
    for (uint16_t i = 0; i < out->doc_count; ++i) {
        uint16_t name_idx = rd16(data + doff);
        const char *src = out->strings[name_idx];
        xmemcpy(out->docs[i].name, src, xstrlen_(src) + 1);
        doff += 8;
    }
    return 0;
}

int ux_attr(const UxNode *n, uint8_t attr_id)
{
    for (uint8_t i = 0; i < n->attr_count; ++i)
        if (n->attr[i] == attr_id)
            return i;
    return -1;
}

int32_t ux_int(const UxFile *f, const UxNode *n, uint8_t attr_id, int32_t def)
{
    (void)f;
    int i = ux_attr(n, attr_id);
    return i < 0 ? def : n->num[i];
}

const char *ux_str(const UxFile *f, const UxNode *n, uint8_t attr_id, const char *def)
{
    int i = ux_attr(n, attr_id);
    if (i < 0 || n->type[i] != 0)
        return def;
    uint16_t idx = (uint16_t)n->num[i];
    if (idx >= f->string_count)
        return def;
    return f->strings[idx];
}

uint32_t ux_clr(const UxFile *f, const UxNode *n, uint8_t attr_id, uint32_t def)
{
    (void)f;
    int i = ux_attr(n, attr_id);
    if (i < 0)
        return def;
    return (uint32_t)n->num[i];
}

const UxDoc *ux_find_doc(const UxFile *f, const char *name)
{
    for (uint16_t i = 0; i < f->doc_count; ++i)
        if (!xstrcmp(f->docs[i].name, name))
            return &f->docs[i];
    return 0;
}

const UxNode *ux_find_name(const UxFile *f, const UxDoc *d, const char *name)
{
    for (uint16_t i = 0; i < f->node_count; ++i) {
        const UxNode *n = &f->nodes[i];
        if (n->parent == UX_NONE && i != d->root)
            continue;
        int a = ux_attr(n, UXA_NAME);
        if (a >= 0 && n->type[a] == 0) {
            uint16_t idx = (uint16_t)n->num[a];
            if (idx < f->string_count && !xstrcmp(f->strings[idx], name))
                return n;
        }
    }
    return 0;
}
