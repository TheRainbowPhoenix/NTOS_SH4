/* Original CP400 TouchKB model: WinCE SIP/WM_CHAR adapter.
 * Coordinates intentionally match NTOS existing phone fingerkb geometry.
 * Four keyboard rows remain in 320x528 (271..528), no dynamic memory,
 * no antialiasing, no external font/renderer dependencies.
 */
#ifndef NTOS_TOUCHKB_H
#define NTOS_TOUCHKB_H
#include <stdint.h>
#define NT_KB_TOP 271
#define NT_KB_BOTTOM 528
#define NT_KB_TEXT_MAX 128
typedef struct {unsigned shift,caps,numbers;} NT_TOUCHKB;
typedef struct {uint16_t text[NT_KB_TEXT_MAX];unsigned length;} NT_KB_EDIT;
static inline void nt_kb_init(NT_TOUCHKB *kb) {
    if(kb){kb->shift=0u;kb->caps=0u;kb->numbers=0u;}
}
static inline void nt_kb_edit_init(NT_KB_EDIT *edit) {
    if(edit){edit->length=0u;edit->text[0]=0u;}
}
static inline unsigned nt_kb_transform(NT_TOUCHKB *kb,unsigned ch) {
    if(!kb)return 0u;
    if(ch>='a' && ch<='z' && (kb->shift^kb->caps))
        ch=ch-'a'+'A';
    if(ch!=0u && kb->shift)kb->shift=0u;
    return ch;
}
static inline unsigned nt_kb_press(NT_TOUCHKB *kb,int x,int y) {
    if(!kb || x<0 || x>=320 || y<NT_KB_TOP || y>=NT_KB_BOTTOM)
        return 0u;
    if(y>=317 && y<366) {
        unsigned col=(unsigned)x/32u;
        const char *row=kb->numbers?"1234567890":"qwertyuiop";
        return nt_kb_transform(kb,(unsigned char)row[col]);
    }
    if(y>=370 && y<418 && x>=16 && x<304) {
        unsigned col=(unsigned)(x-16)/32u;
        const char *row=kb->numbers?"@#$%&*()-":"asdfghjkl";
        if(col<9u)return nt_kb_transform(kb,(unsigned char)row[col]);
    }
    if(y>=422 && y<470) {
        if(x<48) {
            if(!kb->numbers)kb->shift^=1u;
            else kb->numbers=kb->numbers==1u?2u:1u;
            return 0u;
        }
        if(x>=276)return 8u; /* Backspace */
        if(x>=50 && x<274) {
            unsigned col=(unsigned)(x-50)/32u;
            const char *row=kb->numbers?";:/?+=_":"zxcvbnm";
            if(col<7u)
                return nt_kb_transform(kb,(unsigned char)row[col]);
        }
    }
    if(y>=475 && y<525) {
        if(x<49){kb->numbers=kb->numbers?0u:1u;kb->shift=0u;return 0u;}
        if(x>=276)return 13u;
        if(x>=244)return '.';
        if(x>=83 && x<242)return ' ';
        if(x>=50 && x<80)return ',';
    }
    return 0u;
}
static inline int nt_kb_edit_char(NT_KB_EDIT *edit,unsigned cp) {
    if(!edit)return 0;
    if(cp==8u) {
        if(edit->length>0u)
            edit->text[--edit->length]=0u;
        return 1;
    }
    if(cp==13u)return 2; /* Enter is a submit, do not store newline */
    if(cp<32u || cp>126u || edit->length+1u>=NT_KB_TEXT_MAX)
        return 0;
    edit->text[edit->length++]=(uint16_t)cp;
    edit->text[edit->length]=0u;
    return 1;
}
#endif
