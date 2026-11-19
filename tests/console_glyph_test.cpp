// Full-height CP400 launcher glyph smoke test. No SDK/SH4 toolchain needed.
#include "../src/console_font.hpp"
#include "../src/console.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>

static unsigned lit(uint8_t bits) {
    unsigned n=0u;
    for(unsigned bit=0;bit<8u;++bit)
        if(bits&(1u<<bit))++n;
    return n;
}

int main() {
    using namespace ntos_console;
    static_assert(COLUMNS==40u && ROWS==37u);
    static_assert(GLYPH_HEIGHT==14u);
    static_assert(ROWS*GLYPH_HEIGHT==518u);
    static_assert(ROWS*GLYPH_HEIGHT<=LCD_HEIGHT);
    for(unsigned c=32u;c<=126u;++c)
        for(unsigned y=0u;y<GLYPH_HEIGHT;++y)
            assert(glyph_row(c,y)==nt_font_rows[c-32u][y]);
    // With the former y*14/12 mapping, row 6 was never displayed.
    for(unsigned y=0u;y<12u;++y)
        assert(y*14u/12u!=6u);

    assert(glyph_row('E',6u)==0x3eu);
    assert(glyph_row('F',6u)==0x3eu);
    assert(glyph_row('B',6u)==0x1eu);
    assert(lit(glyph_row('E',6u))>=4u);
    assert(lit(glyph_row('F',6u))>=4u);
    assert(lit(glyph_row('B',6u))>=4u);
    assert(glyph_row('C',6u)!=glyph_row('E',6u));
    assert(glyph_row('?',99u)==0u);
    assert(glyph_row(255u,6u)==glyph_row('?',6u));

    // Recreate console glyph placement across actual device LCD geometry.
    // Every source row must land at an independent LCD pixel row.
    uint8_t pixels[LCD_WIDTH*LCD_HEIGHT]{};
    const char *message="EXE: pick again   CLEAR: exit";
    for(unsigned row=0u;row<ROWS;++row) {
        const unsigned top=row*GLYPH_HEIGHT;
        for(unsigned x=0u;message[x] && x<COLUMNS;++x)
            for(unsigned y=0u;y<GLYPH_HEIGHT;++y) {
                const uint8_t mask=glyph_row(static_cast<unsigned char>(message[x]),y);
                for(unsigned bit=0u;bit<GLYPH_WIDTH;++bit)
                    if(mask&(1u<<bit))
                        pixels[(top+y)*LCD_WIDTH+x*GLYPH_WIDTH+bit]=1u;
            }
    }
    for(unsigned x=0u;x<GLYPH_WIDTH;++x)
        assert(pixels[6u*LCD_WIDTH+x]==((glyph_row('E',6u)>>x)&1u));
    // Last displayed row 36 still fits on screen; no row overlap.
    for(unsigned y=0u;y<GLYPH_HEIGHT;++y)
        for(unsigned x=0u;x<GLYPH_WIDTH;++x)
            assert(pixels[((ROWS-1u)*GLYPH_HEIGHT+y)*LCD_WIDTH+x]==
                   ((glyph_row('E',y)>>x)&1u));
    TextConsole<COLUMNS,ROWS> console;
    unsigned painted=0u;
    for(unsigned i=0;i<ROWS+1u;++i)
        console.puts("EXE: pick again", [&](unsigned r,const char *text) {
            assert(r<ROWS && std::strlen(text)<=COLUMNS);
            ++painted;
        });
    assert(console.row()==ROWS-1u && painted>=ROWS+1u);
    std::puts("PASS console 8x14 glyphs: E/F/B middle bars, 37 complete rows, scroll and LCD bounds");
    return 0;
}
