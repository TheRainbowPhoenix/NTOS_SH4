/* CALC.EXE - integer-only pocket calculator using WinCE GDI and input.
 * No floating point operations, CRT or private NTOS imports. */
#include "wince_ui.h"
static const WCHAR16 cls[]={'N','T','C','A','L','C',0};
static const WCHAR16 title[]={'N','T','O','S',' ','C','a','l','c',0};
static const char labels[16][3]={
    "7","8","9","/","4","5","6","*",
    "1","2","3","-","C","0","=","+"
};
static int current=0,acc=0,op=0,new_number=0,error=0;
static unsigned int udiv32(unsigned int n,unsigned int d)
{
    unsigned int q=0,rem=0;
    if (!d) return 0;
    for (int i=31;i>=0;--i) {
        unsigned int hi=(rem>>31);
        rem=(rem<<1)|((n>>(unsigned int)i)&1u);
        if (hi || rem>=d) { rem-=d; q|=1u<<(unsigned int)i; }
    }
    return q;
}
static void as_text(int n,char *buf)
{
    char rev[14];
    unsigned int v;
    int len=0,i=0;
    if (n<0) { buf[i++]='-'; v=(unsigned int)(-(n+1))+1u; }
    else v=(unsigned int)n;
    do {
        unsigned int q=udiv32(v,10u);
        rev[len++]=(char)('0'+(v-q*10u));
        v=q;
    } while(v && len<12);
    while(len) buf[i++]=rev[--len];
    buf[i]=0;
}
static void apply(void)
{
    int a=acc,b=current;
    unsigned int abs_a,abs_b,result;
    int sign=0;
    if (!op) { acc=b; return; }
    if(op=='+') {
        if(a>0 && b>99999999-a) { error=1; return; }
        if(a<0 && b< -99999999-a) { error=1; return; }
        acc=a+b;
    } else if(op=='-') {
        if(a<0 && b>99999999+a) { error=1; return; }
        if(a>0 && b<a-99999999) { error=1; return; }
        acc=a-b;
    } else if(op=='*' || op=='/') {
        sign=(a<0)^(b<0);
        abs_a=(unsigned int)(a<0?-a:a);
        abs_b=(unsigned int)(b<0?-b:b);
        if(op=='/') {
            if(!abs_b) { error=2; return; }
            result=udiv32(abs_a,abs_b);
        } else {
            if(abs_b && abs_a>udiv32(99999999u,abs_b)) {
                error=1; return;
            }
            result=abs_a*abs_b;
        }
        acc=sign?-(int)result:(int)result;
    }
    current=acc;
}
static void press(char key)
{
    if(key=='C') { current=0; acc=0; op=0; new_number=0; error=0; return; }
    if(error) return;
    if(key>='0' && key<='9') {
        if(new_number) { current=0; new_number=0; }
        if(current<9999999) current=current*10+(key-'0');
        return;
    }
    if(key=='+' || key=='-' || key=='*' || key=='/') {
        if(op && !new_number) apply();
        else acc=current;
        op=key;
        new_number=1;
    } else if(key=='=') {
        apply(); op=0; new_number=1;
    }
}
static void draw(void *dc)
{
    char result[16];
    const DWORD bg=RGB(235,238,243),ink=RGB(30,37,53);
    ui_fill(dc,0,0,320,528,bg);
    ui_fill(dc,0,0,320,55,RGB(12,73,132));
    ui_text(dc,17,17,"CALCULATOR",RGB(255,255,255),1);
    ui_text(dc,17,70,"STANDARD  /  INTEGER",RGB(78,88,103),0);
    ui_fill(dc,14,98,306,141,RGB(255,255,255));
    ui_fill(dc,14,140,306,143,RGB(191,197,206));
    if(error) ui_text(dc,28,113,error==2?"DIVIDE BY ZERO":"OVERFLOW",RGB(190,35,49),1);
    else {
        as_text(current,result);
        int n=0;while(result[n]) ++n;
        ui_text(dc,296-n*16,112,result,ink,1);
    }
    for(int i=0;i<16;++i) {
        int c=i%4,r=i/4;
        int l=14+c*75,t=159+r*73;
        DWORD color=i==14?RGB(12,116,194):i==12?RGB(215,221,229):
           c==3?RGB(195,211,228):RGB(255,255,255);
        ui_fill(dc,l,t,l+67,t+65,RGB(160,169,181));
        ui_button(dc,l+1,t+1,l+66,t+63,labels[i],color,
                  i==14?RGB(255,255,255):ink);
    }
    ui_text(dc,15,478,"Touch digits or use keypad",RGB(63,70,81),0);
    ui_text(dc,15,503,"EXE =    C clears    CLEAR exits",RGB(82,89,100),0);
}
static int wndproc(void *hwnd,DWORD msg,DWORD wp,DWORD lp)
{
    char key=0;
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps={0};void *dc=NT_CALL(BeginPaint,(hwnd,&ps));
        if(dc) { draw(dc); NT_CALL(EndPaint,(hwnd,&ps)); }
        return 0;
    }
    if(msg==WM_LBUTTONDOWN) {
        int x=(short)(lp&65535u),y=(short)(lp>>16);
        if(x>=14 && x<314 && y>=159 && y<451) {
            int col=(x-14)/75,row=(y-159)/73;
            if(col<4 && row<4) key=labels[row*4+col][0];
        }
    } else if(msg==WM_KEYDOWN) {
        if(wp>=0x30u && wp<=0x39u) key=(char)wp;
        else if(wp==VK_RETURN) key='=';
        else if(wp=='+' || wp=='-' || wp=='*' || wp=='/') key=(char)wp;
        else if(wp==0x97u || wp=='C') key='C';
    } else return 0;
    if(key) { press(key); NT_CALL(InvalidateRect,(hwnd,0,1)); }
    return 0;
}
int main(void) { ui_init(); return ui_run(cls,title,wndproc); }
