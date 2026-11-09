#!/usr/bin/env python3
"""Offline SVG -> SVF quality preflight, NEVER used on the calculator.

Render each reference SVG and the proposed integer VEC icon at exactly the
sizes actually drawn in NTOS: 17px system rows, 24px chrome, 32px categories
(and optionally 27/36px Start tiles). The preview is an approximation of
UIXRENDER.DLL; geometry is kept integer-only for the SH4 renderer itself.

Requires: python3 -m pip install Pillow CairoSVG
Examples:
 python3 tools/svf_icon_preflight.py --svg payload/phone/svg_reference/system.svg \
     --svf payload/phone/assets/settings/system.svf
 python3 tools/svf_icon_preflight.py --all --root dist
"""
from __future__ import annotations
import argparse
import io
import math
from pathlib import Path
import struct

ARC_SAMPLES = 32
NONE = 0xffff

def u16(blob: bytes, position: int) -> int:
    return struct.unpack_from(">H", blob, position)[0]

def i16(blob: bytes, position: int) -> int:
    return struct.unpack_from(">h", blob, position)[0]

def rgb(pigment: int):
    if pigment == 0x4210:
        return (127, 127, 127, 255)
    if pigment == 0x7fff:
        return (0, 0, 0, 255)
    red, green, blue = ((pigment >> 10) & 31), ((pigment >> 5) & 31), (pigment & 31)
    return (red * 255 // 31, green * 255 // 31, blue * 255 // 31, 255)

def render(blob: bytes, size: int):
    from PIL import Image, ImageDraw
    if blob[:4] not in (b"VEC1", b"VEC2"):
        raise ValueError("Not an SVF icon")
    image = Image.new("RGBA", (size, size), (255, 255, 255, 255))
    draw = ImageDraw.Draw(image)
    vw, vh = 32, 32
    fill = stroke = NONE
    at = 4
    complete = False
    def x(v): return int(v * size / vw)
    def y(v): return int(v * size / vh)
    while at < len(blob):
        op = blob[at]
        at += 1
        if op == 0:
            complete = True
            break
        if op == 0x60:
            vw, vh = u16(blob, at), u16(blob, at+2)
            if not vw or not vh:
                raise ValueError("Invalid viewport")
            at += 4
        elif op == 1:
            fill, stroke = u16(blob, at), u16(blob, at+2)
            at += 4
        elif op == 0x55:
            at += 1 + blob[at]
        elif op == 0x50:
            at += 5 + blob[at+4]
        elif op in (0x10, 0x11):
            a, b, c, d = struct.unpack_from(">hhhh", blob, at)
            width = max(1, int(blob[at+8] * size / vw)) if op == 0x11 else 1
            if fill != NONE:
                draw.rectangle((x(a),y(b),x(c),y(d)), fill=rgb(fill))
            if stroke != NONE:
                draw.rectangle((x(a),y(b),x(c),y(d)), outline=rgb(stroke), width=width)
            at += 9 if op == 0x11 else 8
        elif op in (0x20, 0x21):
            if op == 0x20:
                cx, cy, radius = struct.unpack_from(">hhh", blob, at)
                box=(x(cx-radius),y(cy-radius),x(cx+radius),y(cy+radius))
                at += 6
            else:
                ax, ay, bx, by=struct.unpack_from(">hhhh",blob,at)
                box=(x(ax),y(ay),x(bx),y(by))
                at += 8
            if fill != NONE:
                draw.ellipse(box,fill=rgb(fill))
            if stroke != NONE:
                draw.ellipse(box,outline=rgb(stroke),width=1)
        elif op in (0x30, 0x31):
            a,b,c,d=struct.unpack_from(">hhhh",blob,at)
            width=max(1,min(8,blob[at+8]*size//vw)) if op==0x31 else 1
            if stroke != NONE:
                draw.line((x(a),y(b),x(c),y(d)),fill=rgb(stroke),width=width)
            at += 9 if op == 0x31 else 8
        elif op == 0x22:
            cx,cy,r=struct.unpack_from(">hhh",blob,at)
            start,count,width=blob[at+6:at+9]
            if start>=32 or count>32 or not width:
                raise ValueError("Invalid arc")
            points=[]
            for i in range(count+1):
                angle=2*math.pi*((start+i)%32)/32
                # Match C integer LUT endpoints rather than run trig on device.
                dx=round(math.cos(angle)*1024)
                dy=round(math.sin(angle)*1024)
                points.append((x(cx+(r*dx)//1024),y(cy+(r*dy)//1024)))
            if stroke != NONE and count:
                draw.line(points,fill=rgb(stroke),
                          width=max(1,min(8,width*size//vw)))
            at += 9
        elif op == 0x40:
            count=blob[at]
            at += 1
            if count<3 or count>128:
                raise ValueError("Invalid polygon")
            pts=[]
            for i in range(count):
                pts.append((x(i16(blob,at)),y(i16(blob,at+2))))
                at += 4
            if fill != NONE:
                draw.polygon(pts,fill=rgb(fill))
            if stroke != NONE:
                draw.line(pts+[pts[0]],fill=rgb(stroke),width=1)
        else:
            raise ValueError(f"Unsupported opcode 0x{op:02X}")
    if not complete:
        raise ValueError("Unterminated SVF")
    return image

def compare(svg: Path, svf: Path, output: Path, sizes: tuple[int,...]):
    from PIL import Image, ImageChops, ImageDraw
    import cairosvg
    document=svf.read_bytes()
    rows=[]
    for size in sizes:
        reference=Image.open(io.BytesIO(cairosvg.svg2png(
            bytestring=svg.read_bytes(),output_width=size,output_height=size))).convert("RGBA")
        canvas=Image.new("RGBA",(size,size),(255,255,255,255))
        canvas.alpha_composite(reference)
        target=render(document,size)
        mismatch=ImageChops.difference(canvas.convert("RGB"),target.convert("RGB"))
        brightness=sum(max(pixel) for pixel in mismatch.getdata())
        error=brightness/(size*size*255)
        rows.append((size,canvas,target,mismatch,error))
    zoom=8
    width=max(size for size,*_ in rows)*zoom*3+100
    sheet=Image.new("RGB",(width,sum(size*zoom+56 for size,*_ in rows)),(248,250,252))
    pen=ImageDraw.Draw(sheet)
    y0=5
    for size,reference,target,mismatch,error in rows:
        pen.text((10,y0),f"{svg.stem}: {size}px  RGB mismatch={error:.1%}",fill=(20,30,40))
        top=y0+25
        for col,im in enumerate((reference,target,mismatch)):
            sheet.paste(im.convert("RGB").resize((size*zoom,size*zoom),
                        resample=Image.Resampling.NEAREST),
                        (10+col*(size*zoom+20),top))
        y0 += size*zoom+56
        print(f"{svg.name:<15} {size:>2}px mean RGB difference: {error:.1%}")
    output.parent.mkdir(parents=True,exist_ok=True)
    sheet.save(output)
    print(f"Preflight comparison: {output}")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--svg",type=Path)
    ap.add_argument("--svf",type=Path)
    ap.add_argument("--out",type=Path,default=Path("dist/svf-preflight.png"))
    ap.add_argument("--sizes",type=int,nargs="+",default=[17,24,32])
    ap.add_argument("--all",action="store_true")
    ap.add_argument("--root",type=Path,default=Path("dist"))
    args=ap.parse_args()
    if args.all:
        base=Path("payload/phone")
        candidates=[
            ("system",base/"assets/settings/system.svf"),
            ("devices",base/"assets/settings/devices.svf"),
            ("edge",args.root/"Apps/edge/edge.svf"),
        ]
        for name,svf in candidates:
            if not svf.is_file():
                raise FileNotFoundError(f"Build phone assets first: {svf}")
            compare(base/"svg_reference"/f"{name}.svg",svf,
                    args.out.parent/f"{name}-preflight.png",tuple(args.sizes))
    else:
        if args.svg is None or args.svf is None:
            ap.error("--svg and --svf required unless --all")
        compare(args.svg,args.svf,args.out,tuple(args.sizes))
if __name__=="__main__":
    main()
