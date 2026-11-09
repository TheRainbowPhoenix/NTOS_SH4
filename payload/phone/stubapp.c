/* One real SH4 PE per app, sharing the small shell/application framework. */
#include "phone.h"
#ifndef APP_ID
#define APP_ID 1
#endif
IMP(int, mobile_RunApplication, (const AppConfig *))
IMP(void, menu_StartMenuInitialize, (void))
IMP(void, menu_StartMenuUninitialize, (void))
int main(void)
{
    static AppConfig config;
    config.reserved[0] = APP_ID;
    CALL(menu_StartMenuInitialize, ());
    int rc = CALL(mobile_RunApplication, (&config));
    CALL(menu_StartMenuUninitialize, ());
    return rc;
}
