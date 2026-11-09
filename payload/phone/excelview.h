/* Read-only Excel UI demo. Splash advances on tap/EXE; no workbook engine. */
static int excel_stage, excel_sample, excel_scroll, excel_drag, excel_start, excel_moved,
    excel_row = -1, excel_col;
#define EXCEL_GREEN RGB_(33, 115, 70)
static void excel_reset(void)
{
    excel_stage = excel_sample = excel_scroll = excel_moved = 0;
    excel_row = -1;
}
static void excel_paint(void *dc)
{
    if (!excel_stage) {
        r->fill(dc, 0, 24, 320, view_bottom, EXCEL_GREEN);
        r->text(dc, 145, 186, "Office", WHITE, 11);
        Pt page[4] = {{112, 228}, {170, 216}, {170, 316}, {112, 304}};
        r->polygon(dc, page, 4, WHITE);
        r->fill(dc, 174, 232, 212, 305, WHITE);
        for (int y = 240; y < 303; y += 13)
            r->fill(dc, 176, y, 207, y + 8, EXCEL_GREEN);
        r->fill(dc, 185, 232, 189, 305, WHITE);
        Pt cross[12] = {{126, 248}, {134, 248}, {142, 261}, {149, 248}, {157, 248}, {146, 270},
                        {158, 292}, {149, 292}, {141, 278}, {133, 292}, {125, 292}, {137, 270}};
        r->polygon(dc, cross, 12, EXCEL_GREEN);
        r->text(dc, 52, view_bottom - 35, "Tap or EXE to open sample", WHITE, 12);
        return;
    }
    r->fill(dc, 0, 24, 320, view_bottom, WHITE);
    r->fill(dc, 0, 24, 320, 76, EXCEL_GREEN);
    r->text(dc, 12, 30,
            excel_sample ? "Project Management - Read-only" : "Employee Management - Read-only",
            WHITE, 12);
    for (int i = 0; i < 3; ++i)
        r->line(dc, 17, 55 + i * 5, 36, 55 + i * 5, WHITE);
    r->ellipse(dc, 202, 59, 6, 6, WHITE, 1);
    r->line(dc, 198, 64, 192, 70, WHITE);
    r->fill(dc, 0, 78, 320, 114, RGB_(248, 248, 248));
    r->text(dc, 26, 87, "fx", GRAY, 18);
    r->text(dc, 75, 88, "Excel sample data", GRAY, 14);
    r->fill(dc, 0, 115, 320, 141, RGB_(238, 238, 238));
    const char *letters = "ABC";
    for (int c = 0; c < 3; ++c) {
        char label[2] = {letters[c], 0};
        r->text(dc, 64 + c * 104, 120, label, RGB_(45, 62, 85), 12);
    }
    r->clip(0, 141, 320, view_bottom - 44);
    for (int i = 0; i < 24; ++i) {
        int y = 141 + i * 26 - excel_scroll;
        if (y + 26 < 141 || y >= view_bottom - 44)
            continue;
        r->fill(dc, 0, y, 42, y + 26, RGB_(241, 241, 241));
        r->line(dc, 0, y + 25, 320, y + 25, RGB_(220, 220, 220));
        char number[3] = {(char)('0' + (i + 1) / 10), (char)('0' + (i + 1) % 10), 0};
        r->text(dc, 16, y + 5, i < 9 ? number + 1 : number, RGB_(48, 62, 81), 12);
        for (int c = 0; c < 3; ++c)
            r->line(dc, 42 + c * 104, y, 42 + c * 104, y + 26, RGB_(220, 220, 220));
        if (i == 0 || i == 2)
            r->fill(dc, 69, y + 5, 320, y + 24, RGB_(219, 225, 241));
        if (i == 4) {
            r->fill(dc, 69, y, 320, y + 26, RGB_(36, 35, 91));
            r->text(dc, 71, y + 5, excel_sample ? "Project name" : "Employee ID", WHITE, 12);
            r->text(dc, 177, y + 5, excel_sample ? "Task name" : "Full name", WHITE, 12);
        }
        if (i >= 5) {
            static const char *people[] = {"John Smith",     "Jane Doe",     "Michael Johnson",
                                           "Emily Brown",    "David Wilson", "Lisa Taylor",
                                           "Daniel Martinez"};
            r->text(dc, 71, y + 5, excel_sample ? "Marketing" : "S1001", RGB_(24, 48, 65), 12);
            r->text(dc, 177, y + 5,
                    excel_sample ? (i % 2 ? "Market Research" : "Content Creation")
                                 : people[(i - 5) % 7],
                    RGB_(24, 48, 65), 12);
        }
        if (i == excel_row) {
            int x = 42 + excel_col * 104;
            r->line(dc, x, y, x + 104, y, EXCEL_GREEN);
            r->line(dc, x, y + 25, x + 104, y + 25, EXCEL_GREEN);
            r->line(dc, x, y, x, y + 25, EXCEL_GREEN);
            r->line(dc, x + 103, y, x + 103, y + 25, EXCEL_GREEN);
        }
    }
    r->clip(0, 24, 320, view_bottom);
    r->fill(dc, 0, view_bottom - 44, 320, view_bottom, RGB_(245, 245, 245));
    r->text(dc, 28, view_bottom - 32,
            excel_sample ? "Project Management Data" : "Employee Management Data", RGB_(45, 45, 45),
            14);
}
static int excel_input(U32 msg, U32 wp, int x, int y)
{
    if (msg == WM_KEY_ && wp == 13) {
        excel_stage = 1;
        return 1;
    }
    if (msg == WM_KEY_ && (wp == 0x26 || wp == 0x28)) {
        excel_scroll = clampi(excel_scroll + (wp == 0x28 ? 26 : -26), 0, 400);
        return 1;
    }
    if (msg == WM_DOWN_) {
        excel_drag = y;
        excel_start = excel_scroll;
        excel_moved = 0;
        return 1;
    }
    if (msg == WM_MOVE_) {
        if (y - excel_drag > 8 || excel_drag - y > 8)
            excel_moved = 1;
        if (excel_stage && excel_moved)
            excel_scroll = clampi(excel_start + excel_drag - y, 0, 400);
        return 1;
    }
    if (msg == WM_UP_) {
        if (excel_moved)
            return 1;
        if (!excel_stage) {
            excel_stage = 1;
            return 1;
        }
        if (y < 76 && x < 55) {
            excel_sample = !excel_sample;
            excel_scroll = 0;
            excel_row = -1;
            return 1;
        }
        if (y >= 141 && y < view_bottom - 44 && x >= 42) {
            excel_row = div_(y - 141 + excel_scroll, 26);
            excel_col = clampi(div_(x - 42, 104), 0, 2);
        }
        return 1;
    }
    return 0;
}
