/* Endian-independent PE32 resource reader. PE metadata/resources remain LE. */
#include "../include/ce_types.h"
static uint16_t ce_le16(const unsigned char *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t ce_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int ce_span(size_t at, size_t count, size_t total)
{
    return at <= total && count <= total - at;
}
const unsigned char *CeFindResource(const unsigned char *image, size_t mapped, unsigned type,
                                    unsigned id, size_t *bytes)
{
    if (bytes)
        *bytes = 0;
    if (!image || mapped < 64 || ce_le16(image) != 0x5a4d || type > 65535 || id > 65535)
        return 0;
    uint32_t pe = ce_le32(image + 60);
    if (!ce_span(pe, 24 + 128, mapped) || ce_le32(image + pe) != 0x4550)
        return 0;
    const unsigned char *opt = image + pe + 24;
    if (ce_le16(opt) != 0x10b || ce_le16(image + pe + 20) < 120 || ce_le32(opt + 92) < 3)
        return 0;
    uint32_t root = ce_le32(opt + 112), size = ce_le32(opt + 116);
    if (!size || !ce_span(root, size, mapped))
        return 0;
    size_t offset = 0;
    unsigned keys[3] = {type, id, 0};
    for (unsigned level = 0; level < 3; ++level) {
        if (!ce_span(offset, 16, size))
            return 0;
        const unsigned char *dir = image + root + offset;
        unsigned count = ce_le16(dir + 12) + ce_le16(dir + 14);
        if (!ce_span(offset + 16, (size_t)count * 8, size))
            return 0;
        uint32_t child = 0;
        int found = 0;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned char *entry = dir + 16 + 8 * i;
            uint32_t key = ce_le32(entry);
            if ((level == 2 && !(key & 0x80000000u)) || key == keys[level]) {
                child = ce_le32(entry + 4);
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
        offset = child & 0x7fffffffu;
        if (level < 2) {
            if (!(child & 0x80000000u))
                return 0;
        } else {
            if (child & 0x80000000u || !ce_span(offset, 16, size))
                return 0;
            const unsigned char *entry = image + root + offset;
            uint32_t rva = ce_le32(entry), n = ce_le32(entry + 4);
            if (!ce_span(rva, n, mapped))
                return 0;
            if (bytes)
                *bytes = n;
            return image + rva;
        }
    }
    return 0;
}
int CeLoadString(const unsigned char *image, size_t mapped, unsigned id, CE_WCHAR *dest,
                 int capacity)
{
    if (!dest || capacity <= 0)
        return 0;
    dest[0] = 0;
    size_t bytes = 0;
    const unsigned char *block = CeFindResource(image, mapped, 6, (id >> 4) + 1, &bytes);
    if (!block)
        return 0;
    size_t offset = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (!ce_span(offset, 2, bytes))
            return 0;
        unsigned n = ce_le16(block + offset);
        offset += 2;
        if (!ce_span(offset, (size_t)n * 2, bytes))
            return 0;
        if (i == (id & 15)) {
            unsigned take = n < (unsigned)capacity - 1 ? n : (unsigned)capacity - 1;
            for (unsigned j = 0; j < take; ++j)
                dest[j] = ce_le16(block + offset + 2 * j);
            dest[take] = 0;
            return (int)take;
        }
        offset += (size_t)n * 2;
    }
    return 0;
}
