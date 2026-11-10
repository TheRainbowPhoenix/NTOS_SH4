#!/usr/bin/env python3
"""ascii_frame.py — render a calc screenshot as ASCII art (color-coded) so
the layout can be inspected without a vision model."""
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..',
                                'edge', 'ntos_linux', 'tests'))
from check_shots import read_png

# legend: . page(#F2F2F2)  : shade(#E6E6E6)  # sheet(#DCDCDC)
#         S scrim(84)      B accent blue     K dark(ink)   g gray text
#         w white          C chrome strip    = other
def classify(c):
    r, g, b = c
    if abs(r-242)<10 and abs(g-242)<10 and abs(b-242)<10: return '.'
    if abs(r-230)<10 and abs(g-230)<10 and abs(b-230)<10: return ':'
    if abs(r-220)<10 and abs(g-220)<10 and abs(b-220)<10: return '#'
    if abs(r-84)<25 and abs(g-84)<25 and abs(b-84)<25: return 'S'
    if b > 180 and r < 120: return 'B'
    if r<70 and g<70 and b<70: return 'K'
    if r>240 and g>240 and b>240: return 'w'
    if abs(r-206)<12 and abs(g-207)<12 and abs(b-206)<12: return 'C'
    return '='

def main(path, step=6):
    w, h, data = read_png(path)
    print(f'{path}: {w}x{h} (each char = {step}x{step}px)')
    for y in range(0, h, step):
        row = []
        for x in range(0, w, step):
            i = (y * w + x) * 3
            row.append(classify((data[i], data[i+1], data[i+2])))
        print(''.join(row))

if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 6)
