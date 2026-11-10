/* calc_icons.h — vector operator glyphs for the WindowsCalculator port.
 *
 * The real app draws its operators from Segoe MDL2 private-use glyphs
 * (U+E940..E95x range: divide E94A, multiply E947, minus E949, plus E948,
 * equals E94E, negate E94D, percent E94C, sqrt E94B, backspace E94F) plus
 * composite text (x2, 1/x). The NTOS bitmap font is ASCII-only, so every
 * non-ASCII operator is hand-drawn with the shared files_icons.c stroke
 * helpers in a 100x100 virtual box (same contract as settings_icons.c).
 *
 * Layout reference: calc/analysis/CALCULATOR_UI_REFERENCE.md.
 */
#ifndef CALC_ICONS_H
#define CALC_ICONS_H

#include "files_icons.h" /* IconFillFn + shared stroke helpers */

enum {
    CI_NONE = 0,
    CI_DIVIDE,     /* E94A: bar + two dots              */
    CI_TIMES,      /* E947: 45-degree cross             */
    CI_PLUS,       /* E948: heavy cross                 */
    CI_MINUS,      /* E949: heavy bar                   */
    CI_EQUAL,      /* E94E: two heavy bars              */
    CI_NEGATE,     /* E94D: +/- stacked                 */
    CI_PERCENT,    /* E94C: two circles + slash         */
    CI_SQRT,       /* E94B: radical + vinculum          */
    CI_BACKSPACE,  /* E94F: eraser with X               */
    CI_XPOW2,      /* "x2" composite (superscript)      */
    CI_XPOW3,      /* "x3" composite                    */
    CI_XPOWY,      /* "xy" composite (scientific)       */
    CI_YROOT,      /* "y/x" radical composite           */
    CI_INVERT,     /* "1/x" fraction                    */
    CI_ABS,        /* "|x|"                             */
    CI_TENPOW,     /* "10^x" composite                  */
    CI_EEXP,       /* "e^x" composite                   */
    CI_EXP,        /* "Exp" is text; reserved           */
    CI_LOG,        /* "log" text is fine; reserved      */
    CI_LN,         /* reserved                          */
    CI_FACTORIAL,  /* "n!"                              */
    CI_PI,         /* pi glyph                          */
    CI_EMATH,      /* euler e                           */
    CI_PAREN_L,    /* (                                 */
    CI_PAREN_R,    /* )                                 */
    CI_HISTORY,    /* clock with arrow (Calculator.xbf path) */
    CI_MEM,        /* M with caret (Calculator.xbf path)    */
    CI_TRASH,      /* maps to FI_DELETE                */
    CI_CHEV_L,     /* result-display scroll left       */
    CI_CHEV_R,     /* result-display scroll right      */
    CI_DEG,        /* DEG is text; reserved            */
    CI_COUNT
};

/* name -> id (0 when unknown) */
int ci_find(const char *name);

/* Draw icon `id` centered in the size x size box at (x, y). */
void ci_draw(IconFillFn fill, void *dc, int x, int y, int size,
             unsigned color, int id);

#endif
