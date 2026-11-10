/* User's VEC1/VEC2 command stream: BE int16 coordinates, RGB555 colors. */
#include "phone.h"
IMP(const Render *, render_SpGetThreadContext, (void))
static unsigned word(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}
static int signed_word(const unsigned char *p)
{
    return (int)(short)word(p);
}
static U32 color(unsigned c)
{
    return RGB_(((c >> 10) & 31) * 255 / 31, ((c >> 5) & 31) * 255 / 31, (c & 31) * 255 / 31);
}
static int scan(const unsigned char *b, unsigned n)
{
    if (!b || n < 5 || n > 4096 || b[0] != 'V' || b[1] != 'E' || b[2] != 'C' ||
        (b[3] != '1' && b[3] != '2'))
        return 0;
    unsigned p = 4, commands = 0;
    while (p < n && ++commands <= 512) {
        unsigned op = b[p++], need = 0;
        if (!op)
            return 1;
        if (op == 1 || op == 0x60)
            need = 4;
        else if (op == 0x10 || op == 0x21 || op == 0x30)
            need = 8;
        else if (op == 0x11 || op == 0x22 || op == 0x31)
            need = 9;
        else if (op == 0x20)
            need = 6;
        else if (op == 0x40) {
            if (p >= n || b[p] < 3)
                return 0;
            need = 1u + 4u * b[p];
        } else if (op == 0x55) {
            if (p >= n)
                return 0;
            need = 1u + b[p];
        } else if (op == 0x50) {
            if (n - p < 5)
                return 0;
            need = 5u + b[p + 4];
        } else
            return 0;
        if (need > n - p)
            return 0;
        if ((op == 0x22 || op == 0x31) && (b[p + 8] == 0 || b[p + 8] > 8))
            return 0;
        if (op == 0x22 && (b[p + 6] >= 32 || b[p + 7] > 32 || signed_word(b + p + 4) < 0))
            return 0;
        if (op == 0x60 && (word(b + p) == 0 || word(b + p) > 4096 || word(b + p + 2) == 0 ||
                           word(b + p + 2) > 4096))
            return 0;
        p += need;
    }
    return 0;
}
int SVFValidate(const void *data, unsigned length)
{
    return scan(data, length);
}
/* Optional monochrome recoloring. Most original VEC assets encode white
 * fills for dark tiles. Recolor only neutral near-white pigment, retaining
 * colored illustrations, black outlines and 0xffff transparent sentinels.
 * 0xffffffff disables recoloring for the existing SVFDraw ABI. */
static U32 ink(unsigned rgb555, U32 tint)
{
    unsigned red = (rgb555 >> 10) & 31u;
    unsigned green = (rgb555 >> 5) & 31u;
    unsigned blue = rgb555 & 31u;
    unsigned min = red < green ? red : green;
    if (blue < min)
        min = blue;
    unsigned max = red > green ? red : green;
    if (blue > max)
        max = blue;
    if (tint != 0xffffffffu && rgb555 == 0x4210u)
        return RGB_(127, 127, 127); /* second, 50%-black ink, never alpha-blended */
    if (tint != 0xffffffffu && min >= 22u && max - min <= 3u)
        return tint;
    return color(rgb555);
}
/* 32 angular samples, generated once offline (cos/sin * 1024).
 * ARC uses no floating point, transcendental functions or heap memory. */
static const short arc_x[32] = {1024, 1004, 946,  851,  724,   569,   392,   200,  0,    -200, -392,
                                -569, -724, -851, -946, -1004, -1024, -1004, -946, -851, -724, -569,
                                -392, -200, 0,    200,  392,   569,   724,   851,  946,  1004};
static const short arc_y[32] = {0,    200,   392,   569,   724,  851,  946,  1004, 1024, 1004, 946,
                                851,  724,   569,   392,   200,  0,    -200, -392, -569, -724, -851,
                                -946, -1004, -1024, -1004, -946, -851, -724, -569, -392, -200};
static void stroke_segment(const Render *r, void *dc, int x0, int y0, int x1, int y1,
                           unsigned width, U32 ink_color)
{
    /* Width is in output pixels and capped by the decoder. Center the
     * parallel offsets across the shortest axis, with no trig required. */
    int begin = -(int)((width - 1u) / 2u);
    for (unsigned i = 0; i < width; ++i) {
        int off = begin + (int)i;
        int dx = x1 - x0, dy = y1 - y0;
        if (dx < 0)
            dx = -dx;
        if (dy < 0)
            dy = -dy;
        if (dx >= dy)
            r->line(dc, x0, y0 + off, x1, y1 + off, ink_color);
        else
            r->line(dc, x0 + off, y0, x1 + off, y1, ink_color);
    }
}
static int draw_icon(void *dc, const void *data, unsigned length, int x, int y, int w, int h,
                     U32 tint)
{
    const unsigned char *b = data;
    if (!scan(b, length) || w < 1 || h < 1 || w > 320 || h > 528)
        return 0;
    const Render *r = CALL(render_SpGetThreadContext, ());
    unsigned p = 4, fill = 0xffff, stroke = 0xffff;
    int vw = 32, vh = 32;
#define CX(v) clampi(x + div_((v) * w, vw), -1024, 1024)
#define CY(v) clampi(y + div_((v) * h, vh), -1024, 1024)
    while (p < length) {
        unsigned op = b[p++];
        if (!op)
            return 1;
        if (op == 1) {
            fill = word(b + p);
            stroke = word(b + p + 2);
            p += 4;
            continue;
        }
        if (op == 0x60) {
            vw = (int)word(b + p);
            vh = (int)word(b + p + 2);
            p += 4;
            continue;
        }
        if (op == 0x55) {
            p += 1u + b[p];
            continue;
        }
        if (op == 0x50) {
            char s[256];
            int tx = signed_word(b + p), ty = signed_word(b + p + 2);
            unsigned n = b[p + 4];
            for (unsigned i = 0; i < n; ++i)
                s[i] = (char)b[p + 5 + i];
            s[n] = 0;
            if (stroke != 0xffff)
                r->text(dc, CX(tx), CY(ty), s, ink(stroke, tint), 12);
            p += 5 + n;
            continue;
        }
        if (op == 0x40) {
            unsigned n = b[p++];
            Pt points[255];
            for (unsigned i = 0; i < n; ++i) {
                points[i].x = CX(clampi(signed_word(b + p), -4096, 4096));
                points[i].y = CY(clampi(signed_word(b + p + 2), -4096, 4096));
                p += 4;
            }
            if (fill != 0xffff)
                r->polygon(dc, points, n, ink(fill, tint));
            if (stroke != 0xffff)
                for (unsigned i = 0, j = n - 1; i < n; j = i++)
                    r->line(dc, points[j].x, points[j].y, points[i].x, points[i].y,
                            ink(stroke, tint));
            continue;
        }
        int a = signed_word(b + p), c = signed_word(b + p + 2), d = signed_word(b + p + 4), e = 0;
        if (op != 0x20)
            e = signed_word(b + p + 6);
        if (a < -4096 || a > 4096 || c < -4096 || c > 4096 || d < -4096 || d > 4096 || e < -4096 ||
            e > 4096)
            return 0;
        if (op == 0x31) {
            if (stroke != 0xffff)
                stroke_segment(r, dc, CX(a), CY(c), CX(d), CY(e),
                               (unsigned)clampi(div_((int)b[p + 8] * w, vw), 1, 8),
                               ink(stroke, tint));
            p += 9;
            continue;
        }
        if (op == 0x22) {
            /* ARC: cx,cy,r (BE i16), start sample, length in 1/32 turns,
             * logical stroke width; 32 means a complete circle. */
            unsigned start = b[p + 6], count = b[p + 7], thickness = b[p + 8];
            if (start >= 32 || count > 32 || d < 0)
                return 0;
            if (stroke != 0xffff && count) {
                int oldx = CX(a + (d * arc_x[start]) / 1024);
                int oldy = CY(c + (d * arc_y[start]) / 1024);
                for (unsigned i = 1; i <= count; ++i) {
                    unsigned step = (start + i) & 31u;
                    int newx = CX(a + (d * arc_x[step]) / 1024);
                    int newy = CY(c + (d * arc_y[step]) / 1024);
                    stroke_segment(r, dc, oldx, oldy, newx, newy,
                                   (unsigned)clampi(div_((int)thickness * w, vw), 1, 8),
                                   ink(stroke, tint));
                    oldx = newx;
                    oldy = newy;
                }
            }
            p += 9;
            continue;
        }
        if (op == 0x30) {
            if (stroke != 0xffff)
                r->line(dc, CX(a), CY(c), CX(d), CY(e), ink(stroke, tint));
            p += 8;
            continue;
        }
        if (op == 0x20 || op == 0x21) {
            int cx, cy, rx, ry;
            if (op == 0x20) {
                cx = CX(a);
                cy = CY(c);
                rx = div_(d * w, vw);
                ry = div_(d * h, vh);
                p += 6;
            } else {
                cx = (CX(a) + CX(d)) >> 1;
                cy = (CY(c) + CY(e)) >> 1;
                rx = div_((d - a) * w, 2 * vw);
                ry = div_((e - c) * h, 2 * vh);
                p += 8;
            }
            if (fill != 0xffff)
                r->ellipse(dc, cx, cy, rx, ry, ink(fill, tint), 0);
            if (stroke != 0xffff)
                r->ellipse(dc, cx, cy, rx, ry, ink(stroke, tint), 1);
            continue;
        }
        int l = CX(a), t = CY(c), rr = CX(d), bb = CY(e), bw = op == 0x11 ? b[p + 8] : 1;
        if (fill != 0xffff)
            r->fill(dc, l, t, rr, bb, ink(fill, tint));
        if (stroke != 0xffff) {
            bw = clampi(div_(bw * w, vw), 1, 16);
            r->fill(dc, l, t, rr, t + bw, ink(stroke, tint));
            r->fill(dc, l, bb - bw, rr, bb, ink(stroke, tint));
            r->fill(dc, l, t, l + bw, bb, ink(stroke, tint));
            r->fill(dc, rr - bw, t, rr, bb, ink(stroke, tint));
        }
        p += op == 0x11 ? 9 : 8;
    }
    return 0;
#undef CX
#undef CY
}
int SVFDrawTint(void *dc, const void *data, unsigned length, int x, int y, int w, int h, U32 tint)
{
    return draw_icon(dc, data, length, x, y, w, h, tint);
}
int SVFDraw(void *dc, const void *data, unsigned length, int x, int y, int w, int h)
{
    return draw_icon(dc, data, length, x, y, w, h, 0xffffffffu);
}

int phone_unsupported(void)
{
    return NOTIMPL;
}
#if !defined(PHONE_HOST) && !defined(NTOS_LINUX_BUILD) && !defined(NTOS_APP_BUILD)
int main(void)
{
    return 1;
}
#endif
