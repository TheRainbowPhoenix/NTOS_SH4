/* Native GNU PE export smoke test, freestanding SH4BE. */
__attribute__((used)) int core_add(int a, int b) { return a + b; }
int main(void) { return 1; } /* minimalist DLL entry for linker smoke test */
