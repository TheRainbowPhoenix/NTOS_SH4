/* Executable .EXE image loader for known EX-word ROM32A profiles.
 * Native P1 copy/relocate + resident COREDLL export snap. No P0/UTLB takeover.
 */
#pragma once
#include <stdint.h>
int ntos_exword_run_pe(const char *path, uint32_t *exit_code);
const char *ntos_exword_pe_error(void);
