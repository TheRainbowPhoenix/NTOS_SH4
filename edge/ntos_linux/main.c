#define _GNU_SOURCE
/* main.c — NTOS_SH4_linux runner.
 *
 * Usage:
 *   ntos_linux <payload.dll> [--entry NAME] [--fs DIR] [--script FILE]
 *              [--shot PREFIX] [--shot-all]
 *
 * Loads one or more payload PE DLLs (x86-64, zig-built), patches their
 * NTOS-style __imp_* slots against builtin COREDLL and each other, calls the
 * requested exported entry (default RunApplication) and lets the e2e script
 * drive the app through the CE message loop: script lines execute lazily
 * whenever the app's queue goes idle, so input, SIP toggles and screenshots
 * interleave naturally with the app's paints.
 *
 * Script lines (blank/# ignored):
 *   tap X Y        post WM_DOWN_ + WM_UP_ at (x,y); lparam=(Y<<16)|X
 *   key VK         post WM_KEY_ with wParam=VK (e.g. 0x1b, 0x73)
 *   char C         post WM_CHAR_ with wParam=C
 *   sip on|off     SipShowIM(1/0) + invalidate (payload paints its keyboard)
 *   shot NAME      save PREFIX-NAME.png right away (frame is stable)
 *   puts TEXT      log TEXT
 *   quit           post WM_QUIT (script end implies quit)
 */
#include "ntos_linux.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODULES 32
static PeModule mods[MAX_MODULES];
static size_t mod_count;

static const char *g_shot_prefix = "lcd";

#include <signal.h>
#include <ucontext.h>
static void crash_handler(int sig, siginfo_t *si, void *uc)
{
    ucontext_t *u = uc;
    fprintf(stderr, "SIGSEGV addr=%p RIP=%#llx RSP=%#llx RCX=%#llx RAX=%#llx\n",
            si->si_addr, (unsigned long long)u->uc_mcontext.gregs[REG_RIP],
            (unsigned long long)u->uc_mcontext.gregs[REG_RSP],
            (unsigned long long)u->uc_mcontext.gregs[REG_RCX],
            (unsigned long long)u->uc_mcontext.gregs[REG_RAX]);
    _exit(139);
}
static void install_crash_handler(void)
{
    struct sigaction sa;
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, 0);
}

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

/* ---------------- lazy e2e script engine ---------------- */
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
            return 1; /* messages queued; loop will process them */
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
            return 1; /* state changed; next idle paint reflects it */
        }
        if (sscanf(line, "shot %255s", arg) == 1) {
            char out[600];
            snprintf(out, sizeof(out), "%s-%s.png", g_shot_prefix, arg);
            host_save_png(out);
            host_log("script: saved %s", out);
            continue; /* keep stepping without blocking the app */
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

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <payload.dll> [payload2.dll ...] [--entry NAME] "
                "[--fs DIR] [--script FILE] [--shot PREFIX] [--shot-all]\n",
                argv[0]);
        return 2;
    }
    const char *entry = "RunApplication";
    const char *fs_root = ".";
    const char *script = NULL;
    const char *shot_prefix = "lcd";
    const char *paths[MAX_MODULES];
    size_t npaths = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--entry") && i + 1 < argc)
            entry = argv[++i];
        else if (!strcmp(argv[i], "--fs") && i + 1 < argc)
            fs_root = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc)
            script = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            shot_prefix = argv[++i];
        else
            paths[npaths++] = argv[i];
    }
    if (!npaths) {
        fprintf(stderr, "no payload given\n");
        return 2;
    }
    g_shot_prefix = shot_prefix;
    install_crash_handler();

    host_init(argc, argv, fs_root);

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
        host_log("loaded %s at %p (entry rva %#x, %u exports)",
                 mods[mod_count].name, (void *)mods[mod_count].base,
                 mods[mod_count].entry_rva, mods[mod_count].exp_names_count);
        ++mod_count;
    }

    /* two IAT patch passes so cross-DLL order never matters */
    PeModule *mod_ptrs[MAX_MODULES];
    for (size_t i = 0; i < mod_count; ++i)
        mod_ptrs[i] = &mods[i];
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < mod_count; ++i)
            pe_patch_iat_slots(&mods[i], mod_ptrs, mod_count);

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
    host_log("calling %s!%s", app->name, entry);

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
    }

    typedef MSABI int (*EntryFn)(void);
    EntryFn fn = (EntryFn)(uintptr_t)sym;
    int rc = fn();
    host_log("%s!%s returned %d", app->name, entry, rc);

    char out[600];
    snprintf(out, sizeof(out), "%s-final.png", shot_prefix);
    host_save_png(out);
    host_log("saved %s", out);
    host_shutdown();
    return rc < 0 ? 1 : 0;
}
