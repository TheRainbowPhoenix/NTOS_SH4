// Shared NTOS launcher text geometry and exact UGUI 8x14 glyph rows.
// Never subsample bitmap rows: the center stroke of 'E', 'F' and 'B'
// sits at source row 6. The LCD fits 37 complete 14-pixel console lines.
#pragma once
#include <stdint.h>
#include "../payload/ugui_font.h"

namespace ntos_console {
constexpr unsigned COLUMNS=40u;
constexpr unsigned ROWS=37u;
constexpr unsigned GLYPH_WIDTH=8u;
constexpr unsigned GLYPH_HEIGHT=14u;
constexpr unsigned LCD_WIDTH=320u;
constexpr unsigned LCD_HEIGHT=528u;
static_assert(COLUMNS*GLYPH_WIDTH==LCD_WIDTH);
static_assert(ROWS*GLYPH_HEIGHT<=LCD_HEIGHT);

inline uint8_t glyph_row(unsigned character,unsigned y) {
    if(y>=GLYPH_HEIGHT)return 0u;
    if(character<32u || character>126u)character='?';
    return nt_font_rows[character-32u][y];
}
} // namespace ntos_console
