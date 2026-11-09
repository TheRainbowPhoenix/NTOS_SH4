#include "phone.h"
IMP(const Render *, render_SpGetThreadContext, (void))
static void icon(void *dc, int id, int x, int y, int size, U32 c)
{
    const Render *r = CALL(render_SpGetThreadContext, ());
    /* Paths use a 32x32 integer em box. No icon font or resource DLL. */
#define X(a) (x + div_((a) * size, 32))
#define Y(a) (y + div_((a) * size, 32))
#define F(a, b, d, e) r->fill(dc, X(a), Y(b), X(d), Y(e), c)
#define L(a, b, d, e) r->line(dc, X(a), Y(b), X(d), Y(e), c)
#define E(a, b, rx, ry, t)                                                                         \
    r->ellipse(dc, X(a), Y(b), div_((rx) * size, 32), div_((ry) * size, 32), c,                    \
               (t) ? (size >= 28 ? 2 : 1) : 0)
#define P(...)                                                                                     \
    do {                                                                                           \
        const Pt p[] = {__VA_ARGS__};                                                              \
        r->polygon(dc, p, sizeof(p) / sizeof(p[0]), c);                                            \
    } while (0)
#define V(a, b)                                                                                    \
    {                                                                                              \
        X(a), Y(b)                                                                                 \
    }
    switch (id) {
        case ICON_PHONE:
            P(V(6, 2), V(11, 2), V(15, 10), V(10, 14), V(18, 22), V(22, 17), V(30, 22), V(29, 28),
              V(25, 30), V(17, 27), V(7, 17), V(3, 8));
            break;
        case ICON_MESSAGE:
            F(3, 4, 28, 23);
            P(V(15, 21), V(15, 29), V(24, 21));
            r->fill(dc, X(7), Y(9), X(10), Y(12), BLUE);
            r->fill(dc, X(14), Y(9), X(17), Y(12), BLUE);
            r->fill(dc, X(21), Y(9), X(24), Y(12), BLUE);
            break;
        case ICON_EDGE:
            E(16, 16, 13, 13, 0);
            r->ellipse(dc, X(17), Y(16), div_(9 * size, 32), div_(8 * size, 32), BLUE, 0);
            F(3, 13, 28, 17);
            r->fill(dc, X(21), Y(19), X(31), Y(24), BLUE);
            break;
        case ICON_MAIL:
            L(2, 7, 30, 7);
            L(2, 7, 2, 26);
            L(2, 26, 30, 26);
            L(30, 26, 30, 7);
            L(2, 8, 16, 19);
            L(16, 19, 30, 8);
            break;
        case ICON_CORTANA:
            E(16, 16, 14, 14, 2);
            break;
        case ICON_PHOTO:
            L(2, 4, 30, 4);
            L(30, 4, 30, 29);
            L(30, 29, 2, 29);
            L(2, 29, 2, 4);
            P(V(4, 26), V(12, 14), V(18, 21), V(23, 16), V(29, 26));
            E(23, 10, 3, 3, 0);
            break;
        case ICON_STORE:
            P(V(4, 9), V(28, 5), V(28, 29), V(4, 26));
            E(16, 9, 6, 8, 1);
            r->fill(dc, X(15), Y(13), X(16), Y(25), BLUE);
            r->fill(dc, X(10), Y(19), X(23), Y(20), BLUE);
            break;
        case ICON_MUSIC:
            E(16, 16, 14, 14, 2);
            E(16, 16, 4, 4, 0);
            L(21, 8, 30, 1);
            L(22, 8, 31, 2);
            break;
        case ICON_VIDEO:
            F(3, 12, 29, 30);
            P(V(1, 5), V(28, 0), V(30, 6), V(3, 11));
            r->line(dc, X(8), Y(4), X(12), Y(8), BLUE);
            r->line(dc, X(17), Y(2), X(21), Y(6), BLUE);
            break;
        case ICON_WEATHER:
            E(16, 16, 7, 7, 0);
            L(16, 0, 16, 5);
            L(16, 27, 16, 32);
            L(0, 16, 5, 16);
            L(27, 16, 32, 16);
            L(4, 4, 8, 8);
            L(24, 24, 28, 28);
            L(4, 28, 8, 24);
            L(24, 8, 28, 4);
            break;
        case ICON_ALARM:
            E(16, 18, 11, 11, 2);
            L(16, 18, 16, 10);
            L(16, 18, 22, 20);
            L(4, 5, 10, 2);
            L(23, 2, 29, 5);
            L(9, 28, 6, 32);
            L(23, 28, 26, 32);
            break;
        case ICON_CAMERA:
            F(2, 8, 30, 28);
            F(8, 4, 20, 9);
            r->ellipse(dc, X(16), Y(18), div_(8 * size, 32), div_(8 * size, 32), BLUE, 0);
            break;
        case ICON_PEOPLE:
            E(16, 9, 7, 7, 0);
            E(16, 28, 13, 10, 0);
            break;
        case ICON_SUPPORT:
            E(16, 9, 7, 7, 0);
            E(16, 28, 13, 10, 0);
            F(21, 15, 31, 24);
            break;
        case ICON_CALC:
            F(6, 1, 26, 31);
            r->fill(dc, X(9), Y(4), X(23), Y(10), BLUE);
            for (int yy = 14; yy < 29; yy += 6) {
                for (int xx = 9; xx < 24; xx += 6) {
                    r->fill(dc, X(xx), Y(yy), X(xx + 3), Y(yy + 3), BLUE);
                }
            }
            break;
        case ICON_SETTINGS:
            E(16, 16, 10, 10, 0);
            F(13, 1, 19, 31);
            F(1, 13, 31, 19);
            r->ellipse(dc, X(16), Y(16), div_(5 * size, 32), div_(5 * size, 32), BLUE, 0);
            break;
        case ICON_SEARCH:
            E(13, 13, 10, 10, 1);
            L(20, 21, 30, 31);
            break;
        case ICON_MAP:
            P(V(1, 6), V(11, 1), V(21, 6), V(31, 1), V(31, 26), V(21, 31), V(11, 26), V(1, 31));
            break;
        case ICON_WORD:
        case ICON_POWERPOINT:
        case ICON_EXCEL:
        case ICON_NOTE:
            P(V(2, 3), V(21, 0), V(21, 31), V(2, 28));
            L(23, 6, 30, 6);
            L(30, 6, 30, 27);
            L(23, 27, 30, 27);
            {
                const char *s = id == ICON_WORD         ? "W"
                                : id == ICON_POWERPOINT ? "P"
                                : id == ICON_EXCEL      ? "X"
                                                        : "N";
                r->text(dc, X(6), Y(5), s, BLUE, div_(20 * size, 32));
            }
            break;
        case ICON_NEWS:
            L(2, 3, 30, 3);
            L(2, 3, 2, 29);
            L(30, 3, 30, 29);
            L(2, 29, 30, 29);
            for (int yy = 7; yy < 27; yy += 5) {
                L(6, yy, 26, yy);
            }
            break;
        case ICON_MONEY:
            r->text(dc, X(7), Y(0), "$", c, size);
            break;
        default:
            break;
    }
#undef X
#undef Y
#undef F
#undef L
#undef E
#undef P
#undef V
}
static const Controls controls = {PROFILE, icon};
const Controls *UIXGetSchemaFactory(void)
{
    return &controls;
}
void UIXSchemaUnloaded(void)
{
}
int phone_unsupported(void)
{
    return NOTIMPL;
}
#ifndef PHONE_HOST
int main(void)
{
    return 1;
}
#endif
