/* Exercise the actual COREDLL bridge, including mount-tagged Fugue fds. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define GetLastError nt_test_GetLastError
#define SetLastError nt_test_SetLastError
#define CloseHandle nt_test_CloseHandle
#define LocalFree nt_test_LocalFree
#define NTOS_CORE_HOST_TEST
#define main coredll_dll_entry
#include "../payload/coredll.c"
#undef main
const void *ntos_host_table[28];
static int last_fd,next_fd=0x10001,closed;
static int fake_open(const char *path,u32 flags){assert(path[0]=='\\'&&path[1]!='\\'&&strstr(path,"fls0"));assert(flags==1);return next_fd++;}
static int fake_read(int fd,void *p,u32 n){last_fd=fd;if(n)((char *)p)[0]='P';return n?1:0;}
static int fake_size(int fd){last_fd=fd;return 176;}
static int fake_close(int fd){last_fd=fd;++closed;return 0;}
static u32 fake_event(u32 *x,u32 *y,u32 *dir){(void)x;(void)y;(void)dir;return 2;}
int main(void){
 ntos_host_table[11]=(const void *)fake_open;ntos_host_table[12]=(const void *)fake_read;
 ntos_host_table[14]=(const void *)fake_close;ntos_host_table[17]=(const void *)fake_size;
 u16 path[]={'\\','f','l','s','0','\\','h','o','m','e','.','d','a','t',0};
 void *a=CreateFileW(path,NT_GENERIC_READ,1,0,3,0,0);assert(a!=NT_INVALID_FILE);
 void *b=CreateFileW(path,NT_GENERIC_READ,1,0,3,0,0);assert(b!=a&&b!=NT_INVALID_FILE);
 u16 doubled[]={'\\','\\','f','l','s','0','\\','A','p','p','s','\\','m','a','i','l','\\','m','a','i','l','.','e','x','e',0};
 char normalized[96];assert(narrow_path(doubled,normalized,sizeof(normalized)));
 assert(!strcmp(normalized,"\\fls0\\Apps\\mail\\mail.exe"));
 char data[4];u32 done=0;assert(ReadFile(a,data,4,&done,0)&&done==1&&data[0]=='P'&&last_fd==0x10001);
 assert(GetFileSize(b,0)==176&&last_fd==0x10002);assert(CloseHandle(a)&&last_fd==0x10001);
 assert(!ReadFile(a,data,4,&done,0));assert(CloseHandle(b)&&last_fd==0x10002&&closed==2);
 for(int i=0;i<32;++i)assert(CreateFileW(path,NT_GENERIC_READ,1,0,3,0,0)!=NT_INVALID_FILE);
 assert(CreateFileW(path,NT_GENERIC_READ,1,0,3,0,0)==NT_INVALID_FILE&&GetLastError()==4&&closed==3);
 ntos_host_table[15]=(const void *)fake_event;NT_MSG msg;
 g_class_name[0]=0;assert(GetMessageW(&msg,0,0,0)==0&&msg.message==0x12u);
 const u16 shell[]={'S','t','a','r','t','H','o','s','t',0};copy_wstr(g_class_name,shell,48);
 assert(GetMessageW(&msg,0,0,0)==1&&msg.message==0x100u&&msg.wParam==0x1bu);
 puts("PASS actual COREDLL: full 32-bit SDK descriptors, simultaneous file handles, read/size/close, exhaustion");return 0;
}
