/* win_host.c — NTOS_SH4_windows interactive layer.
 *
 * Owns the desktop window that mirrors the 320x528 device LCD (scaled),
 * translates real mouse/keyboard into the CE message stream the payloads
 * consume (WM_DOWN_/WM_MOVE_/WM_UP_ with lparam = (y<<16)|x, WM_KEY_,
 * WM_CHAR_), and records everything as ntos_linux e2e script lines so a
 * manual bug hunt can be replayed in CI afterwards.
 *
 * Input mapping (identical semantics to the Linux script engine):
 *   left down        -> WM_DOWN_ (wp=1)
 *   left move (held) -> WM_MOVE_ (wp=1)   [scroll drags]
 *   left up          -> WM_UP_   (wp=0)
 *   key down         -> WM_KEY_  (wparam=vk)
 *   key char         -> WM_CHAR_ (wparam=cp)
 *   F12              -> screenshot <prefix>-interactive-N.png (never recorded)
 *   Ctrl+Q / close   -> quit the session (record file is written on exit)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "ntos_linux.h"
#include "win_host.h"

static HWND g_hwnd;
static HDC g_memdc;
static HBITMAP g_bmp;
static HBITMAP g_old_bmp;
static void *g_bits; /* DIB section bits: 0x00RRGGBB order */
static int g_scale = 2;
static int g_alive;
static int g_frame_pending;
static int g_quit_asked;

/* recorder */
static FILE *g_rec;
static int g_rec_count;
static int g_rec_down_x, g_rec_down_y, g_rec_moved;
static int g_shot_seq;
static char g_shot_prefix[256] = "lcd";

/* CE-side queue injection + paint invalidation of the desktop window */
void win_post_ce(U32 msg, U32 wp, U32 lp);

void win_set_shot_prefix(const char *prefix)
{
    snprintf(g_shot_prefix, sizeof(g_shot_prefix), "%s", prefix);
}

void rec_set_file(FILE *f)
{
    g_rec = f;
}

int rec_events(void)
{
    return g_rec_count;
}

static void rec_line(const char *fmt, ...)
{
    if (!g_rec)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_rec, fmt, ap);
    va_end(ap);
    fputc('\n', g_rec);
    fflush(g_rec);
    ++g_rec_count;
}

void rec_tap(int x, int y)
{
    rec_line("tap %d %d", x, y);
}

void rec_drag(int x1, int y1, int x2, int y2)
{
    rec_line("drag %d %d %d %d", x1, y1, x2, y2);
}

void rec_key(unsigned vk)
{
    rec_line("key 0x%x", vk);
}

void rec_char(unsigned ch)
{
    rec_line("char %u", ch);
}

/* ---------------- screenshot (same PNG writer as the Linux host) -------- */
void win_screenshot(const char *prefix)
{
    char out[600];
    snprintf(out, sizeof(out), "%s-interactive-%03d.png", prefix, ++g_shot_seq);
    host_save_png(out);
    host_log("saved %s", out);
    win_invalidate();
}

/* ---------------- device LCD window ---------------- */
static LRESULT CALLBACK device_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (g_memdc && host_fb) {
                /* host_fb is 0x00BBGGRR (COLORREF order); DIB sections want
                 * 0x00RRGGBB — swap R/B on the fly into the DIB bits */
                U32 *dst = (U32 *)g_bits;
                const U32 *src = host_fb;
                size_t n = NTOS_SCREEN_W * NTOS_SCREEN_H;
                for (size_t i = 0; i < n; ++i) {
                    U32 p = src[i];
                    dst[i] = ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu);
                }
                int w = NTOS_SCREEN_W * g_scale, h = NTOS_SCREEN_H * g_scale;
                SetStretchBltMode(dc, COLORONCOLOR);
                StretchBlt(dc, 0, 0, w, h, g_memdc, 0, 0, NTOS_SCREEN_W, NTOS_SCREEN_H, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            g_frame_pending = 0;
            return 0;
        }
        case WM_TIMER:
            /* the 15ms heartbeat: gives get_message_w a chance to repaint even
             * when no input is flowing (animations, scrolls settling) */
            if (g_frame_pending)
                InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            int x = (short)LOWORD(lp) / g_scale;
            int y = (short)HIWORD(lp) / g_scale;
            U32 lparam = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            g_rec_down_x = x;
            g_rec_down_y = y;
            g_rec_moved = 0;
            win_post_ce(WM_DOWN_, 1, lparam);
            SetCapture(hwnd);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!(wp & MK_LBUTTON))
                return 0;
            int x = (short)LOWORD(lp) / g_scale;
            int y = (short)HIWORD(lp) / g_scale;
            U32 lparam = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            int dx = x - g_rec_down_x, dy = y - g_rec_down_y;
            if (dx < -3 || dx > 3 || dy < -3 || dy > 3)
                g_rec_moved = 1;
            win_post_ce(WM_MOVE_, 1, lparam);
            return 0;
        }
        case WM_LBUTTONUP: {
            int x = (short)LOWORD(lp) / g_scale;
            int y = (short)HIWORD(lp) / g_scale;
            U32 lparam = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            win_post_ce(WM_UP_, 0, lparam);
            ReleaseCapture();
            if (g_rec_moved)
                rec_drag(g_rec_down_x, g_rec_down_y, x, y);
            else
                rec_tap(x, y);
            return 0;
        }
        case WM_KEYDOWN: {
            if (wp == VK_F12) { /* screenshot hotkey, never forwarded/recorded */
                win_screenshot(g_shot_prefix);
                return 0;
            }
            if (wp == 'Q' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                win_set_quit(); /* Ctrl+Q: leave the interactive session */
                return 0;
            }
            rec_key((unsigned)wp);
            win_post_ce(WM_KEY_, (U32)wp, 0);
            return 0;
        }
        case WM_CHAR: {
            if (wp >= ' ' && wp < 0x80) {
                rec_char((unsigned)wp);
                win_post_ce(WM_CHAR_, (U32)wp, 0);
            } else if (wp == 0x08 || wp == 0x0d) { /* backspace / enter */
                rec_char((unsigned)wp);
                win_post_ce(WM_CHAR_, (U32)wp, 0);
            }
            return 0;
        }
        case WM_CLOSE:
            win_set_quit();
            return 0;
        case WM_DESTROY:
            g_alive = 0;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int win_auto_scale(void)
{
#ifndef SM_CYWORKAREA
#define SM_CYWORKAREA 48 /* stable Win32 value; zig's headers trim it */
#endif
    int h = GetSystemMetrics(SM_CYWORKAREA);
    /* 528*2 + caption (~30) + margins: only go 2x when there is headroom */
    return (h >= 528 * 2 + 90) ? 2 : 1;
}

int win_window_create(int scale, const char *title)
{
    if (scale <= 0)
        scale = win_auto_scale();
    g_scale = scale;

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = device_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.lpszClassName = L"NTOS_SH4_windows";
    if (!RegisterClassW(&wc))
        return 0;

    RECT r = {0, 0, NTOS_SCREEN_W * g_scale, NTOS_SCREEN_H * g_scale};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"NTOS SH4 — device LCD", style, CW_USEDEFAULT,
                             CW_USEDEFAULT, w, h, NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd)
        return 0;

    HDC dc = GetDC(g_hwnd);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = NTOS_SCREEN_W;
    bi.bmiHeader.biHeight = -NTOS_SCREEN_H; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &g_bits, NULL, 0);
    g_memdc = CreateCompatibleDC(dc);
    g_old_bmp = (HBITMAP)SelectObject(g_memdc, g_bmp);
    ReleaseDC(g_hwnd, dc);

    SetTimer(g_hwnd, 1, 15, NULL);
    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    UpdateWindow(g_hwnd);
    g_alive = 1;
    if (title)
        SetWindowTextA(g_hwnd, title);
    return 1;
}

void win_window_destroy(void)
{
    if (!g_hwnd)
        return;
    KillTimer(g_hwnd, 1);
    if (g_memdc) {
        SelectObject(g_memdc, g_old_bmp);
        DeleteDC(g_memdc);
        g_memdc = NULL;
    }
    if (g_bmp) {
        DeleteObject(g_bmp);
        g_bmp = NULL;
    }
    DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    g_alive = 0;
}

int win_window_alive(void)
{
    return g_alive;
}

void win_invalidate(void)
{
    if (g_hwnd)
        InvalidateRect(g_hwnd, NULL, FALSE);
}

void win_set_quit(void)
{
    g_quit_asked = 1;
    host_request_quit();
}

/* Called by win_core.c after the payload repainted the framebuffer */
void win_frame_ready(void)
{
    if (!g_hwnd)
        return;
    g_frame_pending = 1;
    InvalidateRect(g_hwnd, NULL, FALSE);
    UpdateWindow(g_hwnd); /* the payload expects synchronous feedback */
}

/* Pump Win32 messages while the CE queue is empty. Returns 1 when the pump
 * queued CE work (or a frame is due), 0 when nothing happened. */
int win_pump(void)
{
    if (!g_hwnd)
        return 0;
    MSG msg;
    int worked = 0;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            host_request_quit();
            return 1;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        worked = 1;
    }
    if (g_quit_asked)
        return 1;
    if (worked || g_frame_pending)
        return 1;
    /* idle: block until the next input/timer/paint — this is what makes
     * interactive mode feel native instead of busy-spinning */
    WaitMessage();
    return 1;
}

/* CE queue injection used by the window proc (implemented in win_core.c
 * terms: host_post with the current CE hwnd) */
void win_post_ce(U32 msg, U32 wp, U32 lp)
{
    host_post(msg, wp, lp);
}
