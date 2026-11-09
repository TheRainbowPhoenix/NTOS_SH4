#!/usr/bin/env python3
"""Optional Windows authoring tool. Bake tiny 1-bpp glyphs; no TTF at runtime.
Requires Pillow only when regenerating the checked-in header.
"""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
out=['/* Generated 1-bpp UI glyphs. No TTF parser or font file on device. */',
     'typedef struct {unsigned short offset;unsigned char width,advance;} PhoneGlyph;',
     'typedef struct {int height,first,count;const PhoneGlyph *glyphs;const unsigned char *bits;} PhoneFont;']
fonts=[]
for h,chars in [(9,range(32,127)),(11,range(32,127)),(12,range(32,127)),
                (14,range(32,127)),(18,range(32,127)),(22,range(32,127)),
                (28,range(65,91)),(43,range(56,57)),
                (14,range(0x20ac,0x20ad)),(14,range(0xa3,0xa4)),(14,range(0xa5,0xa6)),
                (18,range(0x20ac,0x20ad)),(18,range(0xa3,0xa4)),(18,range(0xa5,0xa6))]:
 face='segoeuil.ttf' if h>=22 else 'segoeui.ttf'
 font=ImageFont.truetype('C:/Windows/Fonts/'+face,h)
 data=[];glyphs=[]
 for cp in chars:
  ch=chr(cp);advance=max(1,round(font.getlength(ch)));width=min(32,advance+1)
  image=Image.new('L',(width,h));d=ImageDraw.Draw(image)
  d.text((0,h-(3 if h<18 else 4)),ch,font=font,fill=255,anchor='ls')
  glyphs.append((len(data),width,advance));stride=(width+7)//8
  for y in range(h):
   for byte in range(stride):
    data.append(sum((1<<bit) for bit in range(8) if byte*8+bit<width and image.getpixel((byte*8+bit,y))>=112))
 tag=str(h)+'_'+str(chars.start)
 out.append(f'static const PhoneGlyph pg{tag}[]={{'+','.join('{%d,%d,%d}'%g for g in glyphs)+'};')
 out.append(f'static const unsigned char pb{tag}[]={{'+','.join(str(n) for n in data)+'};')
 fonts.append('{%d,%d,%d,pg%s,pb%s}'%(h,chars.start,len(chars),tag,tag))
out.append('static const PhoneFont phone_fonts[]={'+','.join(fonts)+'};')
dest=Path(__file__).with_name('font_bitmap.h');dest.write_text('\n'.join(out)+'\n')
print(dest)
