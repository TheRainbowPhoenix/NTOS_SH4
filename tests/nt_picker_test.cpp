#include "../src/nt/picker.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using ntos_picker::Picker;
static ntos_picker::Action press(Picker &p,uint32_t a,uint32_t b) {
    assert(p.key(0u,0u)==ntos_picker::none);
    return p.key(a,b);
}
int main() {
    Picker p{};
    char row[41];
    ntos_picker::format_row(row,21u,21u,"LONGNAME.EXE",4195840u);
    assert(row[0]=='>' && row[1]==' ' && row[2]==' ' &&
           row[3]=='2' && row[4]=='2' && row[5]==':' && row[40]==0);
    ntos_picker::format_row(row,0u,21u,"HELLO.EXE",123u);
    assert(row[0]==' ' && row[2]==' ' && row[3]==' ' &&
           row[4]=='1');
    ntos_picker::format_row(row,63u,63u,
                           "OVERLONGFILENAME12345678901234567890.EXE",
                           0xFFFFFFFFu);
    assert(row[0]=='>' && row[2]==' ' && row[3]=='6' &&
           row[4]=='4' && row[40]==0);
    ntos_picker::format_row(row,255u,255u,
        "PSSTACK.EXE",2048u);
    assert(row[0]=='>' && row[2]=='2' && row[3]=='5' &&
           row[4]=='6' && row[5]==':' && row[40]==0);
    p.init(64u);
    assert(p.selected==0u && p.first==0u);
    // HH3 physical KEY_2 twice selects the one-based 22nd EXE.
    assert(press(p,0u,0x00080000u)==ntos_picker::redraw);
    assert(p.selected==1u && p.typed==2u && p.typed_digits==1u);
    assert(press(p,0u,0x00080000u)==ntos_picker::redraw);
    assert(p.selected==21u && p.typed==22u && p.typed_digits==2u);
    // Backspace removes the trailing '2', then '1' selects item 21.
    assert(press(p,0x00000080u,0u)==ntos_picker::redraw);
    assert(p.selected==1u && p.typed==2u && p.typed_digits==1u);
    assert(press(p,0u,0x08000000u)==ntos_picker::redraw);
    assert(p.selected==20u && p.typed==21u);
    assert(press(p,0u,0x00400000u)==ntos_picker::redraw);
    assert(p.selected==21u && p.typed_digits==0u);
    assert(press(p,0u,0x00800000u)==ntos_picker::redraw);
    assert(p.selected==20u);
    // No autorepeat on held keys; key-up enables second press.
    p.init(64u);
    assert(p.key(0u,0x00080000u)==ntos_picker::redraw);
    assert(p.key(0u,0x00080000u)==ntos_picker::none);
    assert(p.selected==1u && p.typed_digits==1u);
    assert(p.key(0u,0u)==ntos_picker::none);
    assert(p.key(0u,0x00080000u)==ntos_picker::redraw);
    assert(p.selected==21u);
    // 64th item is visible after page scrolling; old 1-digit
    // formatting would produce a non-digit char for these indices.
    p.init(64u);
    press(p,0x00000010u,0u); // 6
    press(p,0u,0x10000000u); // 4
    assert(p.selected==63u && p.first+ntos_picker::VISIBLE>63u);
    assert(press(p,0x04000000u,0u)==ntos_picker::launch);
    assert(press(p,0x00020000u,0u)==ntos_picker::exit_host);
    assert(press(p,0x00004000u,0u)==ntos_picker::exit_host);
    // 256-entry heap-backed menu still permits direct three-digit jump,
    // without wrapping or launching a truncated index after the 64th EXE.
    p.init(256u);
    press(p,0u,0x00080000u); // 2
    press(p,0u,0x00100000u); // 5
    press(p,0x00000010u,0u); // 6 = item 256
    assert(p.selected==255u && p.typed==256u &&
           p.typed_digits==3u &&
           p.first+ntos_picker::VISIBLE>255u);
    // Fourth digit starts a NEW three-digit query.
    press(p,0u,0x08000000u);
    assert(p.selected==0u && p.typed==1u && p.typed_digits==1u);
    p.init(256u);
    press(p,0u,0x08000000u); // 1
    press(p,0u,0x04000000u); // 0
    press(p,0u,0x04000000u); // 0 => 100
    assert(p.selected==99u && p.typed==100u);
    // Invalid digits never pick an out-of-range entry or wrap silently.
    p.init(23u);
    press(p,0x00000020u,0u); // 9 valid
    press(p,0x00000020u,0u); // 99 invalid
    assert(p.selected==8u && p.typed==99u);
    press(p,0x00000080u,0u);
    assert(p.selected==8u && p.typed==9u);
    press(p,0u,0x04000000u); // 90 out of range
    assert(p.selected==8u && p.typed==90u);
    press(p,0x00000080u,0u);
    assert(p.selected==8u);
    p.init(23u);
    press(p,0u,0x00080000u);
    press(p,0x00000008u,0u); // 23
    assert(p.selected==22u);
    assert(press(p,0x08000000u,0u)==ntos_picker::redraw); // ADD wrap
    assert(p.selected==0u);
    p.init(0u);
    assert(press(p,0u,0x00080000u)==ntos_picker::none);
    // Flash enumeration is not sorted. Compare only indices so the catalog
    // keeps the original filename-size pairing used to launch the PE.
    static ntos_picker::Catalog catalog{};
    const char *unordered[]={
        "WINLAB.EXE","GWESLAB.EXE","DLGCHK.EXE","GDIBLIT.EXE",
        "GWESCHK.EXE","alpha.EXE","ALPHA.EXE","B.EXE"
    };
    constexpr unsigned entries=sizeof(unordered)/sizeof(unordered[0]);
    for(unsigned i=0u;i<entries;++i) {
        std::strcpy(catalog.names[i],unordered[i]);
        catalog.sizes[i]=100u+i;
        catalog.pick[i]=static_cast<int>(i);
    }
    ntos_picker::sort_by_name(catalog,entries);
    const int expected[]={5,6,7,2,3,4,1,0};
    for(unsigned i=0u;i<entries;++i) {
        assert(catalog.pick[i]==expected[i]);
        assert(catalog.sizes[catalog.pick[i]]==100u+static_cast<unsigned>(expected[i]));
    }
    // A second sort is idempotent; equal case-insensitive names stay stable.
    ntos_picker::sort_by_name(catalog,entries);
    for(unsigned i=0u;i<entries;++i)assert(catalog.pick[i]==expected[i]);
    ntos_picker::sort_by_name(catalog,0u);
    ntos_picker::sort_by_name(catalog,1u);
    for(unsigned i=0u;i<entries;++i)assert(catalog.pick[i]==expected[i]);
    static_assert(ntos_picker::MAX_FILES==256u);
    static_assert(sizeof(ntos_picker::Catalog)<12288u);
    std::puts("PASS picker: alphabetic EXE order, 256 discoveries, three-digit 100/256 jump, 29-row scroll, no held repeats");
}
