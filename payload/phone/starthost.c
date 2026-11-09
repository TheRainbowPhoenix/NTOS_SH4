/* Original StartHost's application work, without desktop CRT/ETW startup.
 * Ghidra: starthost.exe 00401605, RunApplication ordinal 15. */
#include "phone.h"
IMP(int,mobile_RunApplication,(const AppConfig *))
IMP(void,menu_StartMenuInitialize,(void))
IMP(void,menu_StartMenuUninitialize,(void))
static const W16 root[]={'r','e','s',':','/','/','S','t','a','r','t','M','e','n','u','!','M','a','i','n','.','u','i','x','#','F','r','a','m','e',0};
static const W16 home[]={'H','o','m','e',0};
int main(void){static AppConfig c;c.root=root;c.page=home;
 c.reserved[8]=2;c.reserved[13]=1;c.reserved[15]=1;
 CALL(menu_StartMenuInitialize,());int rc=CALL(mobile_RunApplication,(&c));
 CALL(menu_StartMenuUninitialize,());return rc;
}
