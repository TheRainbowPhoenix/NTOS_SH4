/* Genuine DLL-to-DLL import, no hand-built PE import descriptor. */
extern int core_add(int, int);
__attribute__((used)) int layer_sum(int value) { return core_add(value, 10); }
int main(void) { return 1; }
