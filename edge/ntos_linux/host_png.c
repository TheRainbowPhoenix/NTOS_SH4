/* host_png.c — dependency-free PNG writer for the NTOS_SH4_linux LCD dump.
 *
 * Emits 8-bit RGB PNGs using stored (uncompressed) deflate blocks: no zlib
 * needed, ~2KB of code, plenty fast for a 320x528 screenshot per frame.
 * Pixels arrive as 0x00BBGGRR (device RGB_ macro order) and are shuffled to
 * RGB byte order on the way out.
 */
#include "ntos_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static U32 crc32_table[256];
static int crc_ready;

static void crc_init(void)
{
    for (U32 n = 0; n < 256; ++n) {
        U32 c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc32_table[n] = c;
    }
    crc_ready = 1;
}

static U32 crc32_update(U32 crc, const unsigned char *buf, size_t len)
{
    if (!crc_ready)
        crc_init();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void write_chunk(FILE *f, const char type[4], const unsigned char *data, U32 len)
{
    unsigned char hdr[8];
    hdr[0] = (len >> 24) & 0xff;
    hdr[1] = (len >> 16) & 0xff;
    hdr[2] = (len >> 8) & 0xff;
    hdr[3] = len & 0xff;
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    U32 crc = crc32_update(0, (const unsigned char *)type, 4);
    crc = crc32_update(crc, data, len);
    unsigned char tail[4];
    tail[0] = (crc >> 24) & 0xff;
    tail[1] = (crc >> 16) & 0xff;
    tail[2] = (crc >> 8) & 0xff;
    tail[3] = crc & 0xff;
    fwrite(tail, 1, 4, f);
}

/* one scanline filter byte + RGB payload per row, stored-deflate wrapped */
void host_save_png(const char *path)
{
    const int w = NTOS_SCREEN_W, h = NTOS_SCREEN_H;
    size_t raw = (size_t)(w * 3 + 1) * h;
    /* stored deflate adds 5 bytes per <=65535-byte block */
    size_t nblocks = (raw + 65534) / 65535;
    size_t zlen = raw + nblocks * 5 + 2 /*zlib hdr*/ + 4 /*adler*/;
    unsigned char *z = malloc(zlen + 16);
    size_t zi = 0;
    z[zi++] = 0x78;
    z[zi++] = 0x01;
    size_t left = raw;
    unsigned char *rawbuf = malloc(raw);
    size_t ri = 0;
    for (int y = 0; y < h; ++y) {
        rawbuf[ri++] = 0; /* filter: none */
        for (int x = 0; x < w; ++x) {
            U32 p = host_fb[y * w + x];
            rawbuf[ri++] = (p) & 0xff;        /* R (RGB_ puts red low) */
            rawbuf[ri++] = (p >> 8) & 0xff;   /* G */
            rawbuf[ri++] = (p >> 16) & 0xff;  /* B */
        }
    }
    while (left) {
        size_t chunk = left > 65535 ? 65535 : left;
        unsigned char hdr[5];
        int final = (chunk == left);
        hdr[0] = final ? 1 : 0;
        hdr[1] = chunk & 0xff;
        hdr[2] = (chunk >> 8) & 0xff;
        hdr[3] = ~chunk & 0xff;
        hdr[4] = (~chunk >> 8) & 0xff;
        memcpy(z + zi, hdr, 5);
        zi += 5;
        memcpy(z + zi, rawbuf + (raw - left), chunk);
        zi += chunk;
        left -= chunk;
    }
    U32 a = 1, b = 0;
    for (size_t i = 0; i < raw; ++i) {
        a = (a + rawbuf[i]) % 65521;
        b = (b + a) % 65521;
    }
    U32 adler = (b << 16) | a;
    z[zi++] = (adler >> 24) & 0xff;
    z[zi++] = (adler >> 16) & 0xff;
    z[zi++] = (adler >> 8) & 0xff;
    z[zi++] = adler & 0xff;

    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        free(z);
        free(rawbuf);
        return;
    }
    static const unsigned char sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13];
    ihdr[0] = (w >> 24) & 0xff; ihdr[1] = (w >> 16) & 0xff;
    ihdr[2] = (w >> 8) & 0xff; ihdr[3] = w & 0xff;
    ihdr[4] = (h >> 24) & 0xff; ihdr[5] = (h >> 16) & 0xff;
    ihdr[6] = (h >> 8) & 0xff; ihdr[7] = h & 0xff;
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* color type RGB */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    write_chunk(f, "IHDR", ihdr, 13);
    write_chunk(f, "IDAT", z, (U32)zi);
    write_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
    free(rawbuf);
}

void host_save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", NTOS_SCREEN_W, NTOS_SCREEN_H);
    for (int y = 0; y < NTOS_SCREEN_H; ++y)
        for (int x = 0; x < NTOS_SCREEN_W; ++x) {
            U32 p = host_fb[y * NTOS_SCREEN_W + x];
            unsigned char rgb[3] = {(unsigned char)(p & 0xff),
                                    (unsigned char)((p >> 8) & 0xff),
                                    (unsigned char)((p >> 16) & 0xff)};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}
