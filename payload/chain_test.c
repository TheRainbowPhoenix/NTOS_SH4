/* Transitive PE dependency test: imports CHAIN only, not COREDLL. */
#include "ntimport.h"
NT_IMPORT(unsigned,chain_echo,(unsigned));
NT_DEFINE(chain_echo)
int main(void) {
    return (int)NT_CALL(chain_echo,(0x24681357u));
}
