#include "../src/nt/picker.hpp"
#include <cassert>
#include <cstdio>
using ntos_picker::Picker;
static ntos_picker::Action press(Picker &p,uint32_t a,uint32_t b) {
    assert(p.key(0u,0u)==ntos_picker::none);
    return p.key(a,b);
}
int main() {
    Picker p{};
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
    std::puts("PASS picker: visible >, 1-based numeric 2->22, backspace 22->2->21, 64 items, no held repeats");
}
