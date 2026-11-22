/* Bounded NTOS native command parser. No CRT, heap or filesystem side effects.
 * Unlike a desktop cmd.exe this intentionally implements no pipes, expansion,
 * redirection, or command interpolation. A path launches a PE through NTOS. */
#ifndef NTOS_CMD_MODEL_H
#define NTOS_CMD_MODEL_H

typedef enum {
    NT_CMD_EMPTY, NT_CMD_HELP, NT_CMD_CLS, NT_CMD_DIR, NT_CMD_RUN,
    NT_CMD_SPLIT, NT_CMD_FULL, NT_CMD_SWAP, NT_CMD_ROTATE,
    NT_CMD_KB, NT_CMD_EXIT, NT_CMD_BAD, NT_CMD_BAD_PATH
} NT_CMD_KIND;

typedef struct {
    NT_CMD_KIND kind;
    char path[96];
} NT_CMD_ACTION;

static inline int nt_cmd_space(char c)
{
    return c == ' ' || c == '\t';
}
static inline char nt_cmd_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}
static inline int nt_cmd_eq(const char *s, unsigned n, const char *word)
{
    unsigned i = 0;
    while (i < n && word[i] && nt_cmd_lower(s[i]) == word[i])
        ++i;
    return i == n && word[i] == 0;
}
static inline int nt_cmd_exe_suffix(const char *p, unsigned n)
{
    return n >= 4 && p[n - 4] == '.' && nt_cmd_lower(p[n - 3]) == 'e' &&
           nt_cmd_lower(p[n - 2]) == 'x' && nt_cmd_lower(p[n - 1]) == 'e';
}
static inline int nt_cmd_path(const char *src, unsigned count, char out[96])
{
    /* Launches are constrained to the calculator volume. Reject '..',
     * control chars, drive prefixes and UNC paths before going near flash. */
    const char prefix[] = "\\fls0\\";
    unsigned at = 0, i = 0, begin = 0;
    if (!count || count >= 84)
        return 0;
    if (count >= 6 && (src[0] == '\\' || src[0] == '/') &&
        nt_cmd_lower(src[1]) == 'f' && nt_cmd_lower(src[2]) == 'l' &&
        nt_cmd_lower(src[3]) == 's' && src[4] == '0' &&
        (src[5] == '\\' || src[5] == '/')) {
        begin = 6;
    } else if (src[0] == '\\' || src[0] == '/') {
        return 0;
    }
    for (i = 0; i < 6; ++i)
        out[at++] = prefix[i];
    if (begin == count)
        return 0;
    for (i = begin; i < count; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c < 33 || c > 126 || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            return 0;
        if (c == '/' || c == '\\') {
            if (i == begin || i + 1 == count || src[i - 1] == '/' || src[i - 1] == '\\')
                return 0;
            out[at++] = '\\';
        } else {
            if (c == '.' && i + 1 < count && src[i + 1] == '.' &&
                (i == begin || src[i - 1] == '/' || src[i - 1] == '\\'))
                return 0;
            out[at++] = (char)c;
        }
    }
    if (!nt_cmd_exe_suffix(src, count)) {
        if (at + 4 >= 96)
            return 0;
        out[at++] = '.';
        out[at++] = 'E';
        out[at++] = 'X';
        out[at++] = 'E';
    }
    out[at] = 0;
    return 1;
}
static inline void nt_cmd_parse(const char *line, NT_CMD_ACTION *a)
{
    unsigned i = 0, start, n;
    if (!a)
        return;
    a->kind = NT_CMD_EMPTY;
    a->path[0] = 0;
    if (!line)
        return;
    while (nt_cmd_space(line[i]))
        ++i;
    start = i;
    while (line[i] && !nt_cmd_space(line[i]) && i < 128)
        ++i;
    n = i - start;
    if (!n)
        return;
    while (nt_cmd_space(line[i]))
        ++i;
    if (nt_cmd_eq(line + start, n, "help") || nt_cmd_eq(line + start, n, "?"))
        a->kind = NT_CMD_HELP;
    else if (nt_cmd_eq(line + start, n, "cls"))
        a->kind = NT_CMD_CLS;
    else if (nt_cmd_eq(line + start, n, "dir"))
        a->kind = NT_CMD_DIR;
    else if (nt_cmd_eq(line + start, n, "split"))
        a->kind = NT_CMD_SPLIT;
    else if (nt_cmd_eq(line + start, n, "full"))
        a->kind = NT_CMD_FULL;
    else if (nt_cmd_eq(line + start, n, "swap"))
        a->kind = NT_CMD_SWAP;
    else if (nt_cmd_eq(line + start, n, "rotate"))
        a->kind = NT_CMD_ROTATE;
    else if (nt_cmd_eq(line + start, n, "kb"))
        a->kind = NT_CMD_KB;
    else if (nt_cmd_eq(line + start, n, "exit"))
        a->kind = NT_CMD_EXIT;
    else if (nt_cmd_eq(line + start, n, "run")) {
        start = i;
        while (line[i] && !nt_cmd_space(line[i]) && i < 128)
            ++i;
        n = i - start;
        a->kind = nt_cmd_path(line + start, n, a->path) ? NT_CMD_RUN : NT_CMD_BAD_PATH;
    } else {
        /* Bare EXE names are first-class commands, as on NT. */
        a->kind = nt_cmd_path(line + start, n, a->path) ? NT_CMD_RUN : NT_CMD_BAD;
    }
    while (nt_cmd_space(line[i]))
        ++i;
    if (line[i] != 0 && a->kind != NT_CMD_BAD_PATH)
        a->kind = NT_CMD_BAD;
}
#endif
