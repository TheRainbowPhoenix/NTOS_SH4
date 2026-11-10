#!/usr/bin/env python3
"""dump_markup_rects.py — compute the absolute hit rectangle of every
Button in a calc markup page (nested Canvas offsets accumulate), so the
e2e script coordinates can be validated."""
import sys
from xml.etree import ElementTree as ET


def walk(el, ox, oy, out):
    x = int(el.attrib.get('x', 0))
    y = int(el.attrib.get('y', 0))
    w = int(el.attrib.get('w', 0))
    h = int(el.attrib.get('h', 0))
    ax, ay = ox + x, oy + y
    if el.tag == 'Button':
        name = el.attrib.get('name', '?')
        cmd = el.attrib.get('command', '')
        out.append((name, cmd, ax, ay, ax + w, ay + h))
    for child in el:
        walk(child, ax, ay, out)


def main(path):
    tree = ET.parse(path)
    out = []
    for child in tree.getroot():
        walk(child, 0, 0, out)
    print(f'== {path}: {len(out)} buttons')
    for name, cmd, x0, y0, x1, y1 in sorted(out, key=lambda r: (r[3], r[2])):
        print(f'  {name:14s} {cmd:12s} ({x0},{y0})-({x1},{y1})  center=({(x0+x1)//2},{(y0+y1)//2})')


if __name__ == '__main__':
    for p in sys.argv[1:]:
        main(p)
