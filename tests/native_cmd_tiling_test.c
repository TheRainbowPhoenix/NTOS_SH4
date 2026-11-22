#include "../payload/nt_cmd_model.h"
#include "../payload/nt_tiling.h"
#include <assert.h>
#include <string.h>

static NT_CMD_ACTION parsed(const char *line)
{
    NT_CMD_ACTION a;
    nt_cmd_parse(line, &a);
    return a;
}
static void parser(void)
{
    NT_CMD_ACTION a;
    a = parsed("   HeLp   ");
    assert(a.kind == NT_CMD_HELP);
    assert(parsed("cls").kind == NT_CMD_CLS);
    assert(parsed("DIR").kind == NT_CMD_DIR);
    assert(parsed("split").kind == NT_CMD_SPLIT);
    assert(parsed("FULL").kind == NT_CMD_FULL);
    assert(parsed("swap").kind == NT_CMD_SWAP);
    assert(parsed("rotate").kind == NT_CMD_ROTATE);
    assert(parsed("kb").kind == NT_CMD_KB);
    assert(parsed("exit").kind == NT_CMD_EXIT);
    assert(parsed("   ").kind == NT_CMD_EMPTY);
    a = parsed("run HELLO.EXE");
    assert(a.kind == NT_CMD_RUN);
    assert(!strcmp(a.path, "\\fls0\\HELLO.EXE"));
    a = parsed("guiwin");
    assert(a.kind == NT_CMD_RUN);
    assert(!strcmp(a.path, "\\fls0\\guiwin.EXE"));
    a = parsed("RUN Apps/Calc/Calc.exe");
    assert(a.kind == NT_CMD_RUN);
    assert(!strcmp(a.path, "\\fls0\\Apps\\Calc\\Calc.exe"));
    a = parsed("run \\fls0\\CONTROL.EXE");
    assert(a.kind == NT_CMD_RUN);
    assert(!strcmp(a.path, "\\fls0\\CONTROL.EXE"));
    assert(parsed("run").kind == NT_CMD_BAD_PATH);
    assert(parsed("run ../evil").kind == NT_CMD_BAD_PATH);
    assert(parsed("run Apps/../evil").kind == NT_CMD_BAD_PATH);
    assert(parsed("run \\other\\evil.exe").kind == NT_CMD_BAD_PATH);
    assert(parsed("run C:/evil.exe").kind == NT_CMD_BAD_PATH);
    assert(parsed("run //server/share").kind == NT_CMD_BAD_PATH);
    assert(parsed("run hello.exe | exit").kind == NT_CMD_BAD);
    assert(parsed("dir && run test.exe").kind == NT_CMD_BAD);
    assert(parsed("run hello.exe extra").kind == NT_CMD_BAD);
    assert(parsed("run hello?.exe").kind == NT_CMD_BAD_PATH);
    assert(parsed("run hello\\\\test").kind == NT_CMD_BAD_PATH);
    char large[161];
    memset(large, 'a', 160);
    large[160] = 0;
    assert(parsed(large).kind == NT_CMD_BAD);
}
static void geometry(void)
{
    NT_TILE_STATE s;
    NT_TILE_RECT a, b;
    nt_tile_init(&s, 320, 528, 24);
    assert(nt_tile_rect(&s, 0, &a) && !nt_tile_rect(&s, 1, &b));
    assert(a.left == 0 && a.top == 24 && a.right == 320 && a.bottom == 528);
    assert(nt_tile_hit(&s, 100, 24) == 0);
    assert(nt_tile_hit(&s, -1, 30) == -1);
    assert(nt_tile_hit(&s, 320, 30) == -1);
    assert(nt_tile_hit(&s, 30, 23) == -1);
    nt_tile_split(&s);
    assert(nt_tile_rect(&s, 0, &a) && nt_tile_rect(&s, 1, &b));
    assert(a.top == 24 && a.bottom == 276 && b.top == 276 && b.bottom == 528);
    assert(a.bottom == b.top && nt_tile_hit(&s, 100, 275) == 0);
    assert(nt_tile_hit(&s, 100, 276) == 1);
    assert(nt_tile_client(&s, 0, &a) && a.top == 46);
    nt_tile_swap(&s);
    assert(nt_tile_hit(&s, 100, 275) == 1);
    assert(nt_tile_hit(&s, 100, 276) == 0);
    nt_tile_rotate(&s);
    assert(nt_tile_rect(&s, 0, &a) && nt_tile_rect(&s, 1, &b));
    assert(a.left == 160 && a.right == 320 && b.left == 0 && b.right == 160);
    assert(nt_tile_hit(&s, 159, 300) == 1);
    assert(nt_tile_hit(&s, 160, 300) == 0);
    nt_tile_work_bottom(&s, 328);
    assert(nt_tile_hit(&s, 200, 327) == 0 && nt_tile_hit(&s, 200, 328) == -1);
    nt_tile_maximize(&s, 1);
    assert(!s.split && s.primary == 1);
    assert(!nt_tile_rect(&s, 0, &a) && nt_tile_rect(&s, 1, &b));
    assert(b.left == 0 && b.right == 320 && b.bottom == 328);
    nt_tile_work_bottom(&s, 528);
    assert(nt_tile_hit(&s, 319, 527) == 1);
    nt_tile_split(&s);
    assert(nt_tile_hit(&s, 319, 527) == 0);
}
static void coverage(void)
{
    NT_TILE_STATE s;
    NT_TILE_RECT r[2];
    const int sizes[][2] = {{320, 528}, {160, 240}, {1024, 768}};
    for (unsigned k = 0; k < 3; ++k) {
        nt_tile_init(&s, sizes[k][0], sizes[k][1], 24);
        nt_tile_split(&s);
        for (unsigned side = 0; side < 2; ++side) {
            for (unsigned swapped = 0; swapped < 2; ++swapped) {
                assert(nt_tile_rect(&s, 0, &r[0]) && nt_tile_rect(&s, 1, &r[1]));
                int area = 0;
                for (int p = 0; p < 2; ++p) {
                    assert(r[p].left >= 0 && r[p].right <= s.width);
                    assert(r[p].top >= s.top && r[p].bottom <= s.bottom);
                    area += (r[p].right - r[p].left) * (r[p].bottom - r[p].top);
                    assert(nt_tile_hit(&s, r[p].left, r[p].top) == p);
                }
                assert(area == s.width * (s.bottom - s.top));
                nt_tile_swap(&s);
            }
            nt_tile_rotate(&s);
        }
    }
}
int main(void)
{
    parser();
    geometry();
    coverage();
    return 0;
}
