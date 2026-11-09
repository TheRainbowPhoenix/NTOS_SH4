/* Small native UIX profile. Private tables are NOT Microsoft's UIX COM ABI.
 * All public module names/ordinals come from the reference PE manifests. */
#ifndef PHONE_H
#define PHONE_H
#include <stdint.h>
#include "ntimport.h"
typedef uint32_t U32;
typedef uint16_t W16;
typedef struct {int left,top,right,bottom;} Rect;
typedef struct {int x,y;} Pt;
#define RGB_(r,g,b) ((U32)((r)|((g)<<8)|((b)<<16)))
#define WHITE RGB_(255,255,255)
#define BLUE RGB_(57,96,255)
#define GRAY RGB_(170,170,170)
#define PROFILE 0x55495831u
#define NOTIMPL ((int)0x80004001u)
#define WM_PAINT_ 15u
#define WM_KEY_ 0x100u
#define WM_DOWN_ 0x201u
#define WM_UP_ 0x202u
#define WM_MOVE_ 0x200u
#define WM_CHAR_ 0x102u
typedef struct Render {
 U32 profile;
 void (*clip)(int,int,int,int);
 void (*fill)(void *,int,int,int,int,U32);
 void (*text)(void *,int,int,const char *,U32,int);
 void (*line)(void *,int,int,int,int,U32);
 void (*ellipse)(void *,int,int,int,int,U32,int);
 void (*polygon)(void *,const Pt *,unsigned,U32);
} Render;
typedef struct Controls {
 U32 profile;
 void (*icon)(void *,int,int,int,int,U32);
} Controls;
typedef struct Chrome {
 U32 profile;
 void (*paint)(void *);
 int (*input)(U32,U32,U32);
 int (*expanded)(void);
} Chrome;
typedef struct Menu {
 U32 profile;
 void (*paint)(void *);
 int (*input)(U32,U32,U32);
 int (*page)(void);
 int (*scroll)(void);
 void (*open)(unsigned);
} Menu;
typedef struct Keyboard {U32 profile;void (*paint)(void *);int (*input)(U32,U32,U32);int (*page)(void);} Keyboard;
typedef struct Settings {U32 profile;void (*paint)(void *);int (*input)(U32,U32,U32);void (*reset)(void);int (*page)(void);} Settings;
typedef struct {U32 size,flags;Rect desktop,sip;U32 data_size;void *data;} SipInfo;
#define MAX_APPS 40
#define ICON_BYTES 512
typedef struct {unsigned short id;unsigned char size,flags;U32 color;
 char name[40],path[96];unsigned short icon_len;unsigned char icon[ICON_BYTES];
 U32 pe_icon_offset,pe_icon_size;unsigned char pe_icon_type;} AppEntry;
typedef struct {unsigned short id;unsigned char size,x,y,reserved;} Pin;
typedef struct Catalog {unsigned count,pin_count;AppEntry apps[MAX_APPS];Pin pins[MAX_APPS];} Catalog;
#define APP_SETTINGS_ID 21u
/* RunApplication receives the 0x48-byte configuration seen in StartHost.
 * SH4 BE pointers replace x86 pointers; narrow UI tables stay private. */
typedef struct AppConfig {const W16 *root,*page;U32 reserved[16];} AppConfig;
typedef struct App {
 U32 profile;
 const W16 *title;
 const Menu *menu;
 const Chrome *chrome;
} App;
/* On-device: grouped, real PE IAT slots. Native QA: direct typed calls. */
#ifdef PHONE_HOST
#define IMP(ret,alias,params) extern ret alias params;
#define CALL(alias,args) alias args
#else
#define IMP(ret,alias,params) NT_IMPORT(ret,alias,params) NT_DEFINE(alias)
#define CALL(alias,args) NT_CALL(alias,args)
#endif
enum {ICON_PHONE,ICON_MESSAGE,ICON_EDGE,ICON_MAIL,ICON_PEOPLE,ICON_CORTANA,
 ICON_PHOTO,ICON_STORE,ICON_MUSIC,ICON_VIDEO,ICON_WEATHER,ICON_NEWS,
 ICON_MONEY,ICON_CALC,ICON_MAP,ICON_WORD,ICON_POWERPOINT,ICON_EXCEL,
 ICON_NOTE,ICON_ALARM,ICON_CAMERA,ICON_SUPPORT,ICON_SETTINGS,ICON_SEARCH};
static inline int clampi(int n,int a,int b){return n<a?a:n>b?b:n;}
/* No libgcc division helpers and no floating-point on this target. */
static inline unsigned udiv_(unsigned n,unsigned d){
 unsigned q=0,r=0;if(!d)return 0;
 for(int i=31;i>=0;--i){unsigned c=r>>31;r=(r<<1)|((n>>(unsigned)i)&1u);
  if(c||r>=d){r-=d;q|=1u<<(unsigned)i;}}
 return q;
}
static inline int div_(int a,int b){unsigned n=a<0?0u-(unsigned)a:(unsigned)a;
 unsigned d=b<0?0u-(unsigned)b:(unsigned)b;unsigned q=udiv_(n,d);
 return (a<0)^(b<0)?-(int)q:(int)q;}
#endif
