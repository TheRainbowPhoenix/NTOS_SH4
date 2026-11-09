#include "phone.h"
IMP(const Render *,render_SpGetThreadContext,(void))
static int sip,nav;
static void paint(void *dc){const Render *r=CALL(render_SpGetThreadContext,());
 r->clip(0,0,320,528);r->fill(dc,0,0,320,24,0);
 for(int i=0;i<5;++i)r->fill(dc,13+3*i,16-2*i,15+3*i,18,GRAY);
 r->line(dc,249,7,266,7,GRAY);r->line(dc,249,16,266,16,GRAY);
 r->line(dc,249,7,249,16,GRAY);r->line(dc,266,7,266,16,GRAY);
 r->fill(dc,251,9,263,15,GRAY);r->fill(dc,267,10,269,14,GRAY);
 r->text(dc,283,4,"8:02",GRAY,12);
}
static const Chrome chrome={PROFILE,paint};
const Chrome *UIXGetSchemaFactory(void){return &chrome;}
void UIXSchemaUnloaded(void){}
int InitializeShellChromeComponents(void){sip=0;nav=0;return 0;}
int GetSipVisibility(void){return sip;}
int SetSipVisibility(int visible){sip=visible!=0;return 0;}
int GetNavigationBarHeight(void){return nav?40:0;}
int GetIsNavigationBarVisible(void){return nav;}
int SetIsNavigationBarVisible(int visible){nav=visible!=0;return 0;}
int GetLockScreenVisible(void){return 0;}
int GetWaitCursorVisible(void){return 0;}
int phone_unsupported(void){return NOTIMPL;}
#ifndef PHONE_HOST
int main(void){return 1;}
#endif
