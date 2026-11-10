/* win_main.c — NTOS_SH4_windows entry point.
 *
 * usage: ntos_windows <payload.dll> [payload2.dll ...] [--entry NAME]
 *        [--fs DIR] [--script FILE] [--shot PREFIX] [--record FILE]
 *        [--scale 1|2]
 *
 * Same module-loading model as the Linux host: map the x86-64 PE payloads,
 * two-pass patch the NTOS-style __imp_* IAT slots (builtins + cross-module),
 * call DllMain-style entries, then run the LAST module's RunApplication.
 *
 * No --script  -> interactive: a desktop window mirrors the device LCD and
 *                 real mouse/keyboard drive the app; everything you do is
 *                 recorded (--record, default session.script) so the exact
 *                 interaction can be replayed in CI as an e2e script.
 * --script     -> headless deterministic replay, byte-identical behaviour
 *                 (and byte-identical PNGs) with ntos_linux.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ntos_linux.h"
#include "win_host.h"

#define MAX_MODULES 32
static PeModule mods[MAX_MODULES];
static size_t mod_count;

static char g_shot_prefix[256] = "lcd";

static void *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc(n ? (size_t)n : 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

/* ---------------- lazy e2e script engine (ntos_linux format) -------- */
static char **script_lines;
static size_t script_count, script_pos;

static int script_step(void)
{
    while (script_pos < script_count) {
        char *line = script_lines[script_pos++];
        while (*line == ' ' || *line == '\t')
            ++line;
        if (*line == '#' || !*line)
            continue;
        int x = 0, y = 0, x2 = 0, y2 = 0;
        unsigned vk = 0;
        char arg[256] = "";
        if (sscanf(line, "tap %d %d", &x, &y) == 2) {
            U32 lp = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            host_post(WM_DOWN_, 1, lp);
            host_post(WM_UP_, 0, lp);
            return 1;
        }
        if (sscanf(line, "drag %d %d %d %d", &x, &y, &x2, &y2) == 4) {
            /* touch drag: DOWN, interpolated MOVEs, UP (scrolls lists) */
            U32 lp = (U32)(((y & 0xffff) << 16) | (x & 0xffff));
            host_post(WM_DOWN_, 1, lp);
            for (int i = 1; i < 8; ++i) {
                int mx = x + (x2 - x) * i / 8, my = y + (y2 - y) * i / 8;
                U32 m = (U32)(((my & 0xffff) << 16) | (mx & 0xffff));
                host_post(WM_MOVE_, 1, m);
            }
            U32 le = (U32)(((y2 & 0xffff) << 16) | (x2 & 0xffff));
            host_post(WM_UP_, 0, le);
            return 1;
        }
        if (sscanf(line, "key %i", (int *)&vk) == 1) {
            host_post(WM_KEY_, vk, 0);
            return 1;
        }
        if (sscanf(line, "char %i", (int *)&vk) == 1) {
            host_post(WM_CHAR_, vk, 0);
            return 1;
        }
        if (sscanf(line, "sip %15s", arg) == 1) {
            host_sip_set(!strcmp(arg, "on"));
            host_log("script: sip %s", arg);
            return 1;
        }
        if (sscanf(line, "shot %255s", arg) == 1) {
            char out[600];
            snprintf(out, sizeof(out), "%s-%s.png", g_shot_prefix, arg);
            host_save_png(out);
            host_log("script: saved %s", out);
            continue;
        }
        if (!strncmp(line, "puts ", 5)) {
            host_log("script: %s", line + 5);
            continue;
        }
        if (!strcmp(line, "quit")) {
            host_request_quit();
            return 0;
        }
        host_log("script: unknown line '%s' (ignored)", line);
    }
    /* script exhausted */
    host_request_quit();
    return 0;
}

/* ---------------- foreground launch hook (CreateProcessW target) --------
 * Same hand-off model as the Linux host: match the image basename stem to
 * a loaded module, re-snap the shared-component IAT slots to the new
 * foreground, call RunApplication synchronously. */
static PeModule *g_foreground;

static void repatch_shared(PeModule *fg)
{
    for (size_t i = 0; i < mod_count; ++i) {
        if (&mods[i] == fg)
            continue;
        PeModule *ordered[MAX_MODULES];
        size_t cnt = 0;
        ordered[cnt++] = fg;
        for (size_t k = 0; k < mod_count && cnt < MAX_MODULES; ++k)
            if (&mods[k] != fg && &mods[k] != &mods[i])
                ordered[cnt++] = &mods[k];
        pe_patch_iat_slots(&mods[i], ordered, cnt);
    }
}

static int launch_target(const char *image, const char *cmdline)
{
    const char *base = image, *slash = image;
    while (*slash) {
        if (*slash == '/' || *slash == '\\')
            base = slash + 1;
        ++slash;
    }
    char stem[64];
    size_t n = 0;
    while (base[n] && base[n] != '.' && n + 1 < sizeof(stem))
        ++n;
    if (!n)
        return 0;
    memcpy(stem, base, n);
    stem[n] = 0;
    PeModule *target = NULL;
    for (size_t i = 0; i < mod_count; ++i) {
        const char *mn = mods[i].name, *mb = mn, *ms = mn;
        while (*ms) {
            if (*ms == '/' || *ms == '\\')
                mb = ms + 1;
            ++ms;
        }
        size_t k = 0;
        while (mb[k] && mb[k] != '.')
            ++k;
        if (k == n && !_strnicmp(mb, stem, n)) {
            target = &mods[i];
            break;
        }
    }
    if (!target) {
        host_log("launch: no loaded module matches '%s' (image %s)", stem, image);
        return 0;
    }
    void *entry = pe_resolve(target, "RunApplication");
    if (!entry) {
        host_log("launch: %s has no RunApplication export", target->name);
        return 0;
    }
    PeModule *caller = g_foreground;
    g_foreground = target;
    host_set_launch_cmdline(cmdline);
    host_log("launch: foreground -> %s (cmdline \"%s\")", target->name, cmdline);
    repatch_shared(target);
    typedef MSABI int (*EntryFn)(void);
    int rc = ((EntryFn)(uintptr_t)entry)();
    host_log("launch: %s!RunApplication returned %d", target->name, rc);
    if (caller) {
        g_foreground = caller;
        repatch_shared(caller);
    }
    return 1;
}

/* keep the console usable when launched from cmd; mirror output to a log */
static void setup_console(void)
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    (void)inst;
    (void)prev;
    (void)show;

    int argc = 0;
    wchar_t **argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
    char **argv = calloc(argc ? argc : 1, sizeof(char *));
    for (int i = 0; i < argc; ++i) {
        size_t need = wcslen(argvw[i]) * 4 + 4;
        argv[i] = malloc(need);
        WideCharToMultiByte(CP_UTF8, 0, argvw[i], -1, argv[i], (int)need, NULL, NULL);
    }
    LocalFree(argvw);

    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <payload.dll> [payload2.dll ...] [--entry NAME] "
                "[--fs DIR] [--script FILE] [--shot PREFIX] [--record FILE] "
                "[--scale 1|2]\n",
                argv[0] ? argv[0] : "ntos_windows");
        return 2;
    }
    const char *entry = "RunApplication";
    const char *fs_root = ".";
    const char *script = NULL;
    const char *record = "session.script";
    int scale = 0; /* auto */
    const char *paths[MAX_MODULES];
    size_t npaths = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--entry") && i + 1 < argc)
            entry = argv[++i];
        else if (!strcmp(argv[i], "--fs") && i + 1 < argc)
            fs_root = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc)
            script = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            snprintf(g_shot_prefix, sizeof(g_shot_prefix), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--record") && i + 1 < argc) {
            record = argv[++i];
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else
            paths[npaths++] = argv[i];
    }
    if (!npaths) {
        fprintf(stderr, "no payload given\n");
        return 2;
    }
    win_set_shot_prefix(g_shot_prefix);
    setup_console();
    FILE *logf = fopen("ntos-win.log", "w");
    host_log_set_file(logf);
    host_log("NTOS_SH4_windows starting (fs=%s, script=%s)", fs_root, script ? script : "-");

    host_init(argc, argv, fs_root);
    host_set_launch_hook(launch_target);

    for (size_t i = 0; i < npaths; ++i) {
        size_t size;
        void *data = read_file(paths[i], &size);
        if (!data) {
            fprintf(stderr, "cannot read %s\n", paths[i]);
            return 1;
        }
        if (pe_load(&mods[mod_count], paths[i], data, size)) {
            fprintf(stderr, "failed to load %s\n", paths[i]);
            return 1;
        }
        free(data);
        host_log("loaded %s at %p (entry rva %#x, %u exports)", mods[mod_count].name,
                 (void *)mods[mod_count].base, mods[mod_count].entry_rva,
                 mods[mod_count].exp_names_count);
        ++mod_count;
    }

    /* two IAT patch passes so cross-DLL order never matters */
    PeModule *mod_ptrs[MAX_MODULES];
    for (size_t i = 0; i < mod_count; ++i)
        mod_ptrs[i] = &mods[i];
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < mod_count; ++i)
            pe_patch_iat_slots(&mods[i], mod_ptrs, mod_count);

    /* the LAST module is the entry app: shared components (keyboard,
     * chrome) must paint with ITS render context from the first frame */
    g_foreground = &mods[mod_count - 1];
    repatch_shared(g_foreground);

    for (size_t i = 0; i < mod_count; ++i)
        pe_call_entry(&mods[i], mods[i].base);

    PeModule *app = &mods[mod_count - 1];
    void *sym = pe_resolve(app, entry);
    if (!sym) {
        static const char *fallbacks[] = {"main", "WWinMain", "WinMain", NULL};
        for (int i = 0; fallbacks[i] && !sym; ++i)
            sym = pe_resolve(app, fallbacks[i]);
    }
    if (!sym) {
        fprintf(stderr, "entry export '%s' not found in %s\n", entry, app->name);
        return 1;
    }

    if (script) {
        size_t size;
        char *data = read_file(script, &size);
        if (!data) {
            fprintf(stderr, "cannot read script %s\n", script);
            return 1;
        }
        char *p = data;
        while (p && *p) {
            char *nl = strchr(p, '\n');
            if (nl)
                *nl = 0;
            script_lines = realloc(script_lines, (script_count + 1) * sizeof(*script_lines));
            script_lines[script_count++] = p;
            p = nl ? nl + 1 : NULL;
        }
        host_set_idle_hook(script_step);
        win_interactive = 0;
    } else {
        /* interactive session: LCD window + input recorder */
        win_interactive = 1;
        FILE *rec = fopen(record, "w");
        if (rec)
            fputs("# ntos_windows recorded session\n", rec);
        rec_set_file(rec);
        if (!win_window_create(scale, app->name))
            host_log("warning: no interactive window (headless run)");
        else
            host_log("interactive: mouse=taps/drags, keys, F12=screenshot, "
                     "Ctrl+Q=quit; recording to %s",
                     record);
    }

    host_log("calling %s!%s", app->name, entry);
    typedef MSABI int (*EntryFn)(void);
    EntryFn fn = (EntryFn)(uintptr_t)sym;
    int rc = fn();
    host_log("%s!%s returned %d", app->name, entry, rc);

    char out[600];
    snprintf(out, sizeof(out), "%s-final.png", g_shot_prefix);
    host_save_png(out);
    host_log("saved %s", out);

    if (win_interactive) {
        rec_set_file(NULL);
        host_log("recorded %d script lines (%s)", rec_events(), record);
    }
    win_window_destroy();
    host_shutdown();
    if (logf)
        fclose(logf);
    return rc < 0 ? 1 : 0;
}
