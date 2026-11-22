/* WinCE DllMain fixture. Export is readable only after ATTACH succeeded.
 * Keep this DLL pure RAM: no filesystem or SDK interaction. */
#include <stdint.h>
static volatile uint32_t state=0u;
uint32_t initgood_state(void) {return state;}
int main(uint32_t module,uint32_t reason,void *reserved) {
    (void)module;(void)reserved;
    if(reason==1u) {state=1u;return 1;}
    if(reason==0u) {state=2u;return 1;}
    return 0;
}
