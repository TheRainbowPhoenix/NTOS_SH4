/* win_host.h — NTOS_SH4_windows internal glue (not for payloads).
 *
 * Layers:
 *   win_core.c  builtin COREDLL twin of host_core.c (see ntos_linux.h API)
 *   win_host.c  this window layer: the interactive device window, the input
 *               recorder and the screenshot helper
 *   win_main.c  WinMain: module loading (shared pe_loader.c), script engine,
 *               interactive loop
 */
#ifndef WIN_HOST_H
#define WIN_HOST_H

#include <stdio.h>

/* win_core.c <-> win_host.c */
extern int win_interactive; /* 1 when no --script drives the app */
int win_pump(void);         /* pump Win32 messages; 1 if CE work may exist */
void win_frame_ready(void); /* framebuffer repainted by the payload */

/* recorder (win_host.c), used by win_main.c on exit */
void rec_set_file(FILE *f);
void rec_tap(int x, int y);
void rec_drag(int x1, int y1, int x2, int y2);
void rec_key(unsigned vk);
void rec_char(unsigned ch);
int rec_events(void); /* count of recorded lines */

/* window lifecycle (called from win_main.c) */
int win_window_create(int scale, const char *title); /* 0 = headless fallback */
void win_window_destroy(void);
int win_window_alive(void);
int win_auto_scale(void); /* 2 when the workarea fits 1056px, else 1 */
void win_invalidate(void);
void win_set_quit(void);

/* hotkey-triggered screenshot */
void win_screenshot(const char *prefix);

/* final-shot prefix bookkeeping */
void win_set_shot_prefix(const char *prefix);

/* log mirroring (win_core.c), used by win_main.c */
void host_log_set_file(FILE *f);

#endif
