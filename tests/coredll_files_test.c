/* Exercise the actual COREDLL bridge, including mount-tagged Fugue fds. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define GetLastError nt_test_GetLastError
#define SetLastError nt_test_SetLastError
#define CloseHandle nt_test_CloseHandle
#define LocalFree nt_test_LocalFree
#define NTOS_CORE_HOST_TEST
#define main coredll_dll_entry
#include "../payload/coredll.c"
#undef main
const void *ntos_host_table[28];
static int last_fd, next_fd = 0x10001, closed;
static int fake_open(const char *path, u32 flags)
{
    assert(path[0] == '\\' && path[1] != '\\' && strstr(path, "fls0"));
    assert(flags == 1);
    return next_fd++;
}
static int fake_read(int fd, void *p, u32 n)
{
    last_fd = fd;
    if (n)
        ((char *)p)[0] = 'P';
    return n ? 1 : 0;
}
static u32 large_read_remaining;
static unsigned large_read_calls;
static int fake_large_read(int fd, void *p, u32 n)
{
    (void)fd;
    assert(n <= 4096u);
    ++large_read_calls;
    if (n > large_read_remaining)
        n = large_read_remaining;
    memset(p, 'X', n);
    large_read_remaining -= n;
    return (int)n;
}
static int fake_size(int fd)
{
    last_fd = fd;
    return 176;
}
static int fake_close(int fd)
{
    last_fd = fd;
    ++closed;
    return 0;
}
static u32 fake_event(u32 *x, u32 *y, u32 *dir)
{
    (void)x;
    (void)y;
    (void)dir;
    return 2;
}
static int paint_calls, lcd_refresh_calls;
static void fake_refresh(void)
{
    ++lcd_refresh_calls;
}
static int test_window_proc(void *hwnd, u32 message, u32 wp, u32 lp)
{
    (void)wp;
    (void)lp;
    if (message == 0x000fu) {
        ++paint_calls;
        EndPaint(hwnd, 0);
    }
    return 0;
}

/* File Explorer service fakes: one shared in-memory "disk" so CopyFileW
 * and the intent-file channel round-trip without touching the host FS. */
static char disk[64];
static u32 disk_len;
static u32 disk_pos;
static char launched_path[96];
static int fake_delete(const char *path)
{
    /* model deletion: the intent file's bytes disappear from the disk */
    (void)path;
    disk_len = 0;
    disk_pos = 0;
    return 0;
}
static int open_disk(const char *path, u32 flags)
{
    (void)path;
    (void)flags;
    disk_pos = 0; /* fresh handle: sequential reads from the start */
    return 0x20001;
}
static int read_disk(int fd, void *p, u32 n)
{
    (void)fd;
    if (n > disk_len - disk_pos)
        n = disk_len - disk_pos;
    memcpy(p, disk + disk_pos, n);
    disk_pos += n;
    return (int)n;
}
static int write_disk(int fd, const void *p, u32 n)
{
    (void)fd;
    if (n > sizeof(disk) - 1)
        n = sizeof(disk) - 1;
    memcpy(disk, p, n);
    disk_len = n;
    disk[n] = 0;
    return (int)n;
}
static int size_disk(int fd)
{
    (void)fd;
    return (int)disk_len;
}
static int fake_launch(const char *path)
{
    snprintf(launched_path, sizeof(launched_path), "%s", path);
    return 1;
}

int main(void)
{
    ntos_host_table[11] = (const void *)fake_open;
    ntos_host_table[12] = (const void *)fake_read;
    ntos_host_table[14] = (const void *)fake_close;
    ntos_host_table[17] = (const void *)fake_size;
    u16 path[] = {'\\', 'f', 'l', 's', '0', '\\', 'h', 'o', 'm', 'e', '.', 'd', 'a', 't', 0};
    void *a = CreateFileW(path, NT_GENERIC_READ, 1, 0, 3, 0, 0);
    assert(a != NT_INVALID_FILE);
    void *b = CreateFileW(path, NT_GENERIC_READ, 1, 0, 3, 0, 0);
    assert(b != a && b != NT_INVALID_FILE);
    u16 doubled[] = {'\\', '\\', 'f', 'l',  's', '0', '\\', 'A', 'p', 'p', 's', '\\', 'm',
                     'a',  'i',  'l', '\\', 'm', 'a', 'i',  'l', '.', 'e', 'x', 'e',  0};
    char normalized[96];
    assert(narrow_path(doubled, normalized, sizeof(normalized)));
    assert(!strcmp(normalized, "\\fls0\\Apps\\mail\\mail.exe"));
    char data[4];
    u32 done = 0;
    assert(ReadFile(a, data, 4, &done, 0) && done == 1 && data[0] == 'P' && last_fd == 0x10001);
    {
        char markup[15000];
        ntos_host_table[12] = (const void *)fake_large_read;
        large_read_remaining = 14000;
        large_read_calls = 0;
        assert(ReadFile(a, markup, sizeof(markup), &done, 0));
        assert(done == 14000 && large_read_calls == 4 && markup[13999] == 'X');
        ntos_host_table[12] = (const void *)fake_read;
    }
    assert(GetFileSize(b, 0) == 176 && last_fd == 0x10002);
    assert(CloseHandle(a) && last_fd == 0x10001);
    assert(!ReadFile(a, data, 4, &done, 0));
    assert(CloseHandle(b) && last_fd == 0x10002 && closed == 2);
    for (int i = 0; i < 32; ++i)
        assert(CreateFileW(path, NT_GENERIC_READ, 1, 0, 3, 0, 0) != NT_INVALID_FILE);
    assert(CreateFileW(path, NT_GENERIC_READ, 1, 0, 3, 0, 0) == NT_INVALID_FILE &&
           GetLastError() == 4 && closed == 3);
    ntos_host_table[15] = (const void *)fake_event;
    NT_MSG msg;
    g_class_name[0] = 0;
    assert(GetMessageW(&msg, 0, 0, 0) == 0 && msg.message == 0x12u);
    const u16 shell[] = {'S', 't', 'a', 'r', 't', 'H', 'o', 's', 't', 0};
    const u16 hosted[] = {'F','i','l','e','E','x','p','l','o','r','e','r',0};
    copy_wstr(g_class_name, hosted, 48);
    assert(is_hosted_phone() && !legacy_enabled());
    copy_wstr(g_class_name, shell, 48);
    assert(GetMessageW(&msg, 0, 0, 0) == 1 && msg.message == 0x100u && msg.wParam == 0x1bu);
    g_class_name[0] = 0;
    legacy_nav_loaded = legacy_nav = 1;
    msg.message = 0x201u;
    msg.pt.x = 60;
    msg.pt.y = 508;
    assert(legacy_nav_input(&msg) && msg.message == 0);
    msg.message = 0x202u;
    assert(legacy_nav_input(&msg) && msg.message == 0x100u && msg.wParam == 0x1bu);
    msg.message = 0x201u;
    msg.pt.x = 160;
    msg.pt.y = 508;
    legacy_nav_input(&msg);
    msg.message = 0x202u;
    msg.pt.y = 450;
    assert(legacy_nav_input(&msg) && msg.message == 0);
    legacy_nav = 0;
    msg.message = 0x201u;
    msg.pt.y = 508;
    assert(!legacy_nav_input(&msg) && msg.message == 0x201u);
    /* Synchronous UpdateWindow must not queue a second paint or LCD flip.
     * Multiple InvalidateRect calls coalesce until one dispatch. */
    ntos_host_table[2] = (const void *)fake_refresh;
    static const u16 test_class[] = {'T', 'e', 's', 't', 0};
    static const u16 no_title[] = {0};
    copy_wstr(g_class_name, test_class, 48);
    g_class_proc = test_window_proc;
    void *hwnd = CreateWindowExW(0, test_class, no_title, 0, 0, 0, 320, 528, 0, 0, 0, 0);
    assert(hwnd);
    ShowWindow(hwnd, 1);
    assert(UpdateWindow(hwnd) && paint_calls == 1 && lcd_refresh_calls == 1);
    assert(InvalidateRect(hwnd, 0, 1));
    assert(InvalidateRect(hwnd, 0, 1));
    assert(UpdateWindow(hwnd) && paint_calls == 2 && lcd_refresh_calls == 2);
    assert(GetMessageW(&msg, hwnd, 0, 0) == 0); /* no stale queued WM_PAINT */
    assert(InvalidateRect(hwnd, 0, 1));
    assert(GetMessageW(&msg, hwnd, 0, 0) == 1 && msg.message == 0x000fu);
    assert(DispatchMessageW(&msg) == 0);
    assert(paint_calls == 3 && lcd_refresh_calls == 3);
    DestroyWindow(hwnd);

    /* ---- File Explorer services: CopyFileW / RemoveDirectoryW and the
     * CreateProcessW -> intent.dat -> GetCommandLineW activation pair ---- */
    {
        /* the exhaustion block above left all 32 file slots busy: free a
         * few through the tag-encoded handles before the copy tests */
        for (int i = 0; i < 4; ++i)
            CloseHandle((void *)(uintptr_t)(NT_HANDLE_TAG + i + 1u));
        disk_len = 0;
        disk[0] = 0;
        launched_path[0] = 0;
        ntos_host_table[11] = (const void *)open_disk;
        ntos_host_table[12] = (const void *)read_disk;
        ntos_host_table[13] = (const void *)write_disk;
        ntos_host_table[14] = (const void *)fake_close;
        ntos_host_table[17] = (const void *)size_disk;
        ntos_host_table[19] = (const void *)fake_delete;
        /* stage a source through the same fake disk: copy reads it back */
        u16 src[] = {'\\', 'f', 'l', 's', '0', '\\', 'a', '.', 't', 'x', 't', 0};
        u16 dst[] = {'\\', 'f', 'l', 's', '0', '\\', 'b', '.', 't', 'x', 't', 0};
        /* CopyFileW: fake_read serves the staged bytes, fake_write sinks */
        memcpy(disk, "COPYDATA", 8);
        disk_len = 8;
        disk_pos = 0;
        assert(CopyFileW(src, dst, 0));
        /* RemoveDirectoryW routes through the kernel unlink slot */
        u16 dir[] = {'\\', 'f', 'l', 's', '0', '\\', 'e', 'm', 'p', 't', 'y', 0};
        assert(RemoveDirectoryW(dir));
        /* CreateProcessW persists the command line for the next process
         * (through the \fls0\intent.dat channel) and launches the image */
        ntos_host_table[27] = (const void *)fake_launch;
        u16 edge[] = {'\\', 'f', 'l', 's', '0', '\\', 'A', 'p', 'p', 's', '\\',
                      'e', 'd', 'g', 'e', '\\', 'e', 'd', 'g', 'e', '.', 'e',
                      'x', 'e', 0};
        u16 arg[] = {'f', 'i', 'l', 'e', ':', '/', '/', '\\', 'f', 'l', 's', '0',
                     '\\', 'n', 't', 'o', 's', '.', 'h', 't', 'm', 'l', 0};
        assert(CreateProcessW(edge, arg, 0, 0, 0, 0, 0, 0, 0, 0));
        assert(!strcmp(launched_path, "\\fls0\\Apps\\edge\\edge.exe"));
        /* the intent file now holds the URL: the "next process" reads it
         * once through GetCommandLineW and the channel is consumed */
        assert(disk_len >= 7 && !strncmp(disk, "file://", 7));
        g_cmdline_read = 0; /* simulate a fresh process */
        const u16 *cl = GetCommandLineW();
        assert(cl && cl[0] == 'f' && cl[1] == 'i' && cl[2] == 'l' && cl[3] == 'e');
        u32 i;
        for (i = 0; arg[i]; ++i)
            assert(cl[i] == arg[i]);
        assert(cl[i] == 0);
        g_cmdline_read = 0;
        ntos_host_table[19] = (const void *)fake_delete; /* delete succeeded */
        const u16 *cl2 = GetCommandLineW();
        assert(cl2 && cl2[0] == 0); /* consumed: single-shot activation */
        ntos_host_table[27] = 0;
    }
    puts("PASS actual COREDLL: full 32-bit SDK descriptors, simultaneous file handles, "
         "read/size/close, exhaustion");
    return 0;
}
