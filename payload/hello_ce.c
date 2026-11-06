/* Real freestanding SH4BE MZ/PE32 demo with one COREDLL import.
 * NTOSABI1 calling convention: int main(NtosApi*,char**,char**)
 * The loader resolves the global function pointer through the IAT.
 */
typedef void (*output_debug_fn)(const char *);
output_debug_fn volatile imp_OutputDebugStringA = (output_debug_fn)1;
int main(const void *api,char **argv,char **envp) {
    (void)api;(void)argv;(void)envp;
    imp_OutputDebugStringA("NTOS SH4BE PE + COREDLL import works!");
    return 0x13572468;
}
