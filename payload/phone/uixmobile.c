#include "phone.h"
IMP(int,engine_UIXInitializeApplication,(const App *))
IMP(int,engine_UIXLoadMarkup,(const W16 *,void **))
IMP(int,engine_UIXRunApplication,(void))
IMP(void,engine_UIXShutdownApplication,(void))
IMP(const Menu *,menu_UIXGetSchemaFactory,(void))
IMP(const Chrome *,chrome_UIXGetSchemaFactory,(void))
static const W16 title[]={'S','t','a','r','t',0};
int RunApplication(const AppConfig *config){
 static App a={PROFILE,title,0,0};void *view=0;
 a.menu=CALL(menu_UIXGetSchemaFactory,());a.chrome=CALL(chrome_UIXGetSchemaFactory,());
 if(!a.menu||!a.chrome||a.menu->profile!=PROFILE||a.chrome->profile!=PROFILE)return NOTIMPL;
 int rc=CALL(engine_UIXInitializeApplication,(&a));if(rc<0)return rc;
 if(config&&config->root){rc=CALL(engine_UIXLoadMarkup,(config->root,&view));
  if(rc<0){CALL(engine_UIXShutdownApplication,());return rc;}}
 rc=CALL(engine_UIXRunApplication,());CALL(engine_UIXShutdownApplication,());return rc;
}
int GetResolutionWidth(void){return 320;}
int GetResolutionHeight(void){return 528;}
int ThemeGetSystemScreenSize(void){return (320<<16)|528;}
int ThemeGetCurrentHighContrast(void){return 0;}
int ThemeGetSystemHighContrast(void){return 0;}
int ThemeGetRegisteredThemesAndAccentsCount(void){return 1;}
int phone_unsupported(void){return NOTIMPL;}
#ifndef PHONE_HOST
int main(void){return 1;}
#endif
