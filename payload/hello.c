/* hello_ntos.c -- "hello world" payload for the NTOS HH3 loader.
 *
 * Built as a big-endian SH4 COFF EXE (machine 0x01A8) through the MSVC -B2
 * backend route (recreate/examples/msvc_hello_sh4al/build.cmd -- keep
 * relocations enabled; do NOT use /FIXED so NTOS can relocate) and renamed
 * to *.NTX on \fls0\.
 *
 * NTOS entry contract (r4-r7, same as the .sh4al scalar ABI):
 *   int main(const void **api, char **argv, char **envp)
 * The first argument is NTOS's syscall table:
 *   api[0] puts(s)
 *   api[1] printf(x, y, invert, 0, fmt, ...)
 *   api[2] lcd_refresh()
 *   api[3] malloc(n) / api[4] free(p) / api[5] get_key()
 */
typedef unsigned int u32;

typedef void (*puts_fn)(const char *s);

int main(u32 api, char **argv, char **envp)
{
    (void)argv;
    (void)envp;

    if (api == 0)
        return 1;

    puts_fn puts_ = (puts_fn)((const void **)api)[0];

    puts_("Hello from a big-endian SH4 COFF EXE!");
    puts_("running under NTOS on Hollyhock-3.");

    return 0xC0FFEE42;
}
