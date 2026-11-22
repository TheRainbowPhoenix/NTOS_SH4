#include "../payload/wince/nt_touchkb.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    NT_TOUCHKB kb;NT_KB_EDIT edit;
    nt_kb_init(&kb);nt_kb_edit_init(&edit);
    assert(nt_kb_press(&kb,-1,321)==0 && nt_kb_press(&kb,320,321)==0);
    assert(nt_kb_press(&kb,0,270)==0 && nt_kb_press(&kb,0,528)==0);
    assert(nt_kb_press(&kb,1,320)=='q');
    assert(nt_kb_press(&kb,35,320)=='w');
    assert(nt_kb_press(&kb,20,375)=='a');
    assert(nt_kb_press(&kb,50,428)=='z');
    assert(nt_kb_press(&kb,77,428)=='z'); /* same key 50..81 */
    assert(nt_kb_press(&kb,10,430)==0 && kb.shift==1u);
    assert(nt_kb_press(&kb,1,320)=='Q' && !kb.shift);
    assert(nt_kb_press(&kb,1,320)=='q');
    assert(nt_kb_press(&kb,10,490)==0 && kb.numbers==1u);
    assert(nt_kb_press(&kb,0,320)=='1');
    assert(nt_kb_press(&kb,32,320)=='2');
    assert(nt_kb_press(&kb,10,430)==0 && kb.numbers==2u);
    assert(nt_kb_press(&kb,10,490)==0 && kb.numbers==0u);
    assert(nt_kb_press(&kb,100,490)==' ');
    assert(nt_kb_press(&kb,251,490)=='.');
    assert(nt_kb_press(&kb,286,430)==8u);
    assert(nt_kb_press(&kb,290,490)==13u);
    assert(nt_kb_edit_char(&edit,'H')==1);
    assert(nt_kb_edit_char(&edit,'i')==1);
    assert(edit.length==2u && edit.text[0]=='H' && edit.text[1]=='i');
    assert(nt_kb_edit_char(&edit,13u)==2 && edit.length==2u);
    assert(nt_kb_edit_char(&edit,8u)==1 && edit.length==1u);
    assert(nt_kb_edit_char(&edit,8u)==1 && edit.length==0u);
    assert(nt_kb_edit_char(&edit,8u)==1 && edit.length==0u);
    for(unsigned n=0;n<1000u;++n)nt_kb_edit_char(&edit,'x');
    assert(edit.length==NT_KB_TEXT_MAX-1u &&
           edit.text[NT_KB_TEXT_MAX-1u]==0u);
    assert(nt_kb_edit_char(&edit,'X')==0);
    assert(nt_kb_edit_char(&edit,0x20ACu)==0);
    for(unsigned n=0;n<1000u;++n)nt_kb_edit_char(&edit,8u);
    assert(edit.length==0u && edit.text[0]==0u);
    puts("PASS TouchKB: WinCE SIP geometry, case/shift, symbols, backspace, enter, 1000-char overflow saturation and bounded UTF-16 edit buffer");
}
