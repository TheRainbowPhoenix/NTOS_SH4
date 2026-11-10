# edge/tools/work — developer scratch

`mdhost_dbg.c` — native (gcc, MD_STATIC) debug host for the shared
MARKDOWN engine: loads md/html/text, lays out at a view width and dumps
every paint op with coordinates. Includes the real bitmap-font metrics
from payload/phone/font_bitmap.h so runs match the on-device layout
1:1. Build:

    gcc -O0 -g -std=c11 -DMD_STATIC \
      -I../../../payload/phone -I../../../payload \
      -o mdhost_dbg mdhost_dbg.c ../../../payload/phone/markdown.c -lm
    ./mdhost_dbg md ../../../app/tests/assets/article.md

Put referenced svf files in the CWD (the host read_file is relative).
Nothing here ships; it is layout forensics for engine work.
