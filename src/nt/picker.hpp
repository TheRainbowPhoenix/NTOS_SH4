// Physical-key NTOS EXE picker: 1-based three-digit quick jump.
// HH3 getKey() supplies two level bitfields (verified against calc.h).
// No heap, no SDK, no filesystem or background scanning during selection.
// Rendering is deliberately separate; callers show '>' at selected index.
#pragma once
#include <stdint.h>
namespace ntos_picker {
constexpr unsigned VISIBLE=29u; // Debug rows 4..32, status/help at 1..3
// The menu catalog lives in the P1 heap: never allocate this 8.5 KiB
// buffer on the 4 KiB host C stack or in the 64 KiB HH3 .bss.
constexpr unsigned MAX_FILES=256u;
constexpr unsigned MAX_NAME=25u;
struct Catalog {
    char names[MAX_FILES][MAX_NAME+1];
    uint32_t sizes[MAX_FILES];
    int pick[MAX_FILES];
};
static_assert(sizeof(Catalog)<12288u);

// Sort only the existing catalog indices; keep sizes/filenames paired and
// avoid an additional large allocation on the 4 KiB HH3 host stack.
// ASCII-only comparison is intentional: storage filenames are narrow and
// the picker must not depend on a locale or C runtime.
inline bool filename_less(const char *a,const char *b) {
    for(unsigned i=0u;i<=MAX_NAME;++i) {
        unsigned x=static_cast<unsigned char>(a[i]);
        unsigned y=static_cast<unsigned char>(b[i]);
        if(x>='A' && x<='Z')x+=unsigned('a'-'A');
        if(y>='A' && y<='Z')y+=unsigned('a'-'A');
        if(x!=y)return x<y;
        if(x==0u)return false;
    }
    return false;
}
inline void sort_by_name(Catalog &catalog,unsigned count) {
    if(count>MAX_FILES)count=MAX_FILES;
    // Stable insertion sort. Up to 256 files, no recursion, no heap,
    // and no movement of the backing filenames or size records.
    for(unsigned i=1u;i<count;++i) {
        const int item=catalog.pick[i];
        unsigned j=i;
        while(j>0u && filename_less(catalog.names[item],
                                    catalog.names[catalog.pick[j-1u]])) {
            catalog.pick[j]=catalog.pick[j-1u];
            --j;
        }
        catalog.pick[j]=item;
    }
}
enum Action : uint8_t { none=0, redraw=1, launch=2, exit_host=3 };
// 40-column padded row; actual physical glyph selection is explicit.
// Three fixed-width decimal digits render 1..256 without overflowing the
// display; truncate filenames to preserve Debug_Printf's 40-column width.
inline void format_row(char (&line)[41],unsigned zero_based,
                       unsigned selected,const char *name,uint32_t bytes) {
    for(unsigned i=0u;i<40u;++i)line[i]=' ';
    line[40]=0;
    unsigned pos=0u;
    const unsigned number=zero_based+1u;
    line[pos++]=zero_based==selected ? '>' : ' ';
    line[pos++]=' ';
    line[pos++]=number>=100u ? char('0'+(number/100u)%10u) : ' ';
    line[pos++]=number>=10u ? char('0'+(number/10u)%10u) : ' ';
    line[pos++]=char('0'+number%10u);
    line[pos++]=':';
    line[pos++]=' ';
    if(name)
        for(unsigned i=0u;name[i] && pos<31u;++i)
            line[pos++]=name[i];
    if(pos<40u)line[pos++]=' ';
    char reversed[12];
    unsigned len=0u;
    do {
        reversed[len++]=char('0'+bytes%10u);
        bytes/=10u;
    }while(bytes && len<12u);
    while(len && pos<40u)line[pos++]=reversed[--len];
}
struct Picker {
    unsigned count=0u, selected=0u, first=0u;
    unsigned typed=0u,typed_digits=0u;
    uint32_t prior1=0u,prior2=0u;
    void init(unsigned size) {
        count=size;selected=0u;first=0u;
        typed=typed_digits=0u;prior1=prior2=0u;
    }
    void reveal() {
        if(selected<first)first=selected;
        if(selected>=first+VISIBLE)first=selected-VISIBLE+1u;
    }
    int digit(uint32_t fresh1,uint32_t fresh2) const {
        // Keys1: 3=bit3, 6=bit4, 9=bit5.
        // Keys2: 0/1/2/4/5/7/8 (HH3 physical numeric keypad).
        if(fresh2&0x04000000u)return 0;
        if(fresh2&0x08000000u)return 1;
        if(fresh2&0x00080000u)return 2;
        if(fresh1&0x00000008u)return 3;
        if(fresh2&0x10000000u)return 4;
        if(fresh2&0x00100000u)return 5;
        if(fresh1&0x00000010u)return 6;
        if(fresh2&0x20000000u)return 7;
        if(fresh2&0x00200000u)return 8;
        if(fresh1&0x00000020u)return 9;
        return -1;
    }
    Action key(uint32_t held1,uint32_t held2) {
        const uint32_t fresh1=held1&~prior1;
        const uint32_t fresh2=held2&~prior2;
        prior1=held1;prior2=held2;
        if(!count || (!fresh1 && !fresh2))return none;
        // EXE and power/CLEAR take priority; LEFT remains a legacy exit.
        if(fresh1&0x04000000u)return launch;
        if(fresh1&(0x00020000u|0x00004000u))return exit_host;
        if(fresh1&0x00000080u) { // BACKSPACE removes exactly one digit
            if(typed_digits) {
                typed/=10u;
                --typed_digits;
                if(typed_digits && typed>=1u && typed<=count)
                    selected=typed-1u;
                reveal();
            }
            return redraw;
        }
        const int d=digit(fresh1,fresh2);
        if(d>=0) {
            // Three digits for up to 256 EXEs, two for <=99. A further
            // digit begins a new query; held keys cannot repeat.
            const unsigned max_digits=count>99u?3u:2u;
            if(typed_digits>=max_digits){typed=0u;typed_digits=0u;}
            typed=typed*10u+static_cast<unsigned>(d);
            ++typed_digits;
            // Zero/out-of-range is visible in "Jump:" but never selects a
            // nonexistent EXE or accidentally executes an old file index.
            if(typed>=1u && typed<=count)selected=typed-1u;
            reveal();
            return redraw;
        }
        const bool backward=(fresh2&0x00800000u)!=0u; // UP
        const bool forward=(fresh1&0x08000000u)!=0u ||
                           (fresh2&0x00400000u)!=0u; // ADD/DOWN
        if(backward || forward) {
            typed=typed_digits=0u;
            selected=backward ? (selected+count-1u)%count :
                                (selected+1u)%count;
            reveal();
            return redraw;
        }
        return none;
    }
};
} // namespace ntos_picker
