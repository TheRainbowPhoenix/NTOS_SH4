/* Bounded loader for import-free SH4-BE sample DLLs; PE headers stay LE.
 * Images execute from the SH4 uncached P2 alias, avoiding stale instruction cache.
 * This deliberately does not claim support for arbitrary dependent CE DLLs. */
typedef struct CE_MODULE {
    struct CE_MODULE *next;
    void *allocation;
    unsigned char *image;
    u32 size, refs;
} CE_MODULE;
static CE_MODULE *ce_modules;
static CE_MODULE *ce_loaded(void *image)
{
    for (CE_MODULE *m = ce_modules; m; m = m->next)
        if (m->image == image)
            return m;
    return 0;
}
static int ce_windows_path(const u16 *name, u16 *out, unsigned capacity)
{
    static const char prefix[] = "\\fls0\\Windows\\";
    unsigned i = 0, j = 0;
    if (!name)
        return 0;
    while (name[i] == '\\')
        ++i;
    const char *w = "Windows";
    unsigned n = 0;
    while (w[n] && ((name[i + n] | 32) == (unsigned char)(w[n] | 32)))
        ++n;
    if (w[n] || name[i + n] != '\\')
        return 0;
    i += n;
    while (name[i] == '\\')
        ++i;
    while (prefix[j]) {
        out[j] = (unsigned char)prefix[j];
        ++j;
    }
    while (name[i] && j + 1 < capacity)
        out[j++] = name[i++];
    out[j] = 0;
    return name[i] == 0;
}
void *FindFirstFileW(const u16 *pattern, NT_FIND_DATA *data)
{
    u16 mapped[96];
    return ntos_FindFirstFileW(ce_windows_path(pattern, mapped, 96) ? mapped : pattern, data);
}
void *LoadLibraryW(const u16 *name)
{
    u16 mapped[96];
    char path[96];
    const void **a = ntapi();
    unsigned char *raw = 0, *image = 0;
    void *allocation = 0;
    int fd = -1;
    CE_MODULE *module = 0;
    if (!a || !a[11] || !a[12] || !a[14] || !a[17] ||
        !narrow_path(ce_windows_path(name, mapped, 96) ? mapped : name, path, 96))
        goto fail;
    fd = ((int (*)(const char *, u32))a[11])(path, 1);
    if (fd < 0)
        goto fail;
    int length = ((int (*)(int))a[17])(fd);
    if (length < 256 || length > 131072)
        goto fail;
    raw = ce_alloc((u32)length);
    if (!raw)
        goto fail;
    for (u32 pos = 0; pos < (u32)length;) {
        u32 chunk = (u32)length - pos;
        if (chunk > 4096)
            chunk = 4096;
        int got = ((int (*)(int, void *, u32))a[12])(fd, raw + pos, chunk);
        if (got <= 0 || (u32)got > chunk)
            goto fail;
        pos += (u32)got;
    }
    ((int (*)(int))a[14])(fd);
    fd = -1;
    if (ce_le16(raw) != 0x5a4d)
        goto fail;
    u32 pe = ce_le32(raw + 60);
    if (!ce_span(pe, 248, (u32)length) || ce_le32(raw + pe) != 0x4550 ||
        ce_le16(raw + pe + 4) != 0x1a8)
        goto fail;
    unsigned char *opt = raw + pe + 24;
    u32 size = ce_le32(opt + 56), headers = ce_le32(opt + 60), base = ce_le32(opt + 28);
    if (ce_le16(opt) != 0x10b || ce_le16(raw + pe + 20) < 224 || size < headers || size > 131072 ||
        !ce_span(0, headers, (u32)length) || ce_le32(opt + 108))
        goto fail;
    allocation = ce_alloc(size);
    module = ce_alloc(sizeof(*module));
    if (!allocation || !module)
        goto fail;
    image = (unsigned char *)(((u32)allocation & 0x1fffffffu) | 0xa0000000u);
    ce_zero(image, size);
    ce_copy(image, raw, headers);
    u32 sections = ce_le16(raw + pe + 6), table = pe + 24 + ce_le16(raw + pe + 20);
    if (!ce_span(table, sections * 40, (u32)length))
        goto fail;
    for (u32 i = 0; i < sections; ++i) {
        unsigned char *s = raw + table + i * 40;
        u32 at = ce_le32(s + 12), bytes = ce_le32(s + 16), file = ce_le32(s + 20);
        if (!ce_span(at, bytes, size) || !ce_span(file, bytes, (u32)length))
            goto fail;
        ce_copy(image + at, raw + file, bytes);
    }
    u32 reloc = ce_le32(opt + 136), count = ce_le32(opt + 140), delta = (u32)image - base;
    if (!count || !ce_span(reloc, count, size))
        goto fail;
    for (u32 at = reloc; at < reloc + count;) {
        if (!ce_span(at, 8, reloc + count))
            goto fail;
        u32 page = ce_le32(image + at), bytes = ce_le32(image + at + 4);
        if (bytes < 8 || (bytes & 1) || !ce_span(at, bytes, reloc + count))
            goto fail;
        for (u32 j = 8; j < bytes; j += 2) {
            u32 r = ce_le16(image + at + j), kind = r >> 12, target = page + (r & 4095);
            if (!kind)
                continue;
            if (kind != 3 || (target & 3) || !ce_span(target, 4, size))
                goto fail;
            *(u32 *)(image + target) += delta;
        }
        at += bytes;
    }
    module->allocation = allocation;
    module->image = image;
    module->size = size;
    module->refs = 1;
    module->next = ce_modules;
    ce_modules = module;
    ce_free(raw);
    return image;
fail:
    if (fd >= 0)
        ((int (*)(int))a[14])(fd);
    ce_free(raw);
    ce_free(allocation);
    ce_free(module);
    g_last_error = 193;
    return 0;
}
void *GetProcAddressW(void *handle, const u16 *name)
{
    CE_MODULE *m = ce_loaded(handle);
    if (!m || !name)
        goto missing;
    unsigned char *im = m->image;
    u32 pe = ce_le32(im + 60), rva = ce_le32(im + pe + 24 + 96);
    if (!ce_span(rva, 40, m->size))
        goto missing;
    u32 functions = ce_le32(im + rva + 20), names = ce_le32(im + rva + 24),
        eat = ce_le32(im + rva + 28), nt = ce_le32(im + rva + 32), ot = ce_le32(im + rva + 36);
    if (functions > 4096 || names > 4096 || !ce_span(eat, functions * 4, m->size) ||
        !ce_span(nt, names * 4, m->size) || !ce_span(ot, names * 2, m->size))
        goto missing;
    for (u32 i = 0; i < names; ++i) {
        u32 at = ce_le32(im + nt + i * 4), n = 0;
        while (at + n < m->size && n < 128 && im[at + n] && name[n] && im[at + n] == name[n])
            ++n;
        if (at + n >= m->size || n == 128 || im[at + n] || name[n])
            continue;
        u32 index = ce_le16(im + ot + i * 2);
        if (index >= functions)
            goto missing;
        u32 target = ce_le32(im + eat + index * 4);
        if (!target || !ce_span(target, 2, m->size))
            goto missing;
        return im + target;
    }
missing:
    g_last_error = 127;
    return 0;
}
int FreeLibrary(void *handle)
{
    CE_MODULE **link = &ce_modules;
    while (*link && (*link)->image != handle)
        link = &(*link)->next;
    if (!*link) {
        g_last_error = 6;
        return 0;
    }
    CE_MODULE *m = *link;
    *link = m->next;
    ce_free(m->allocation);
    ce_free(m);
    return 1;
}
