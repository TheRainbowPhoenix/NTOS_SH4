#!/usr/bin/env python3
"""verify_fs.py — assert the e2e file operations really happened.

Runs after the scripted UI session and inspects the fake \\fls0\\ tree:
the rename, the move and the copy must all be physically on disk (this is
the "moving files actually moves files" guarantee, on top of the pixel
checks).
"""
import os
import sys

root = sys.argv[1]
fails = 0


def expect(cond, what):
    global fails
    if cond:
        print(f'[OK] {what}')
    else:
        print(f'[FAIL] {what}')
        fails += 1


# rename: Videos -> movies (typed on the shared SIP)
expect(not os.path.isdir(f'{root}/Videos'), 'Videos folder renamed away')
expect(os.path.isdir(f'{root}/movies'), 'movies folder exists (rename)')
# move: data.dat moved from the root into Documents
expect(not os.path.isfile(f'{root}/data.dat'), 'data.dat left the root (move)')
expect(os.path.isfile(f'{root}/Documents/data.dat'), 'Documents/data.dat exists (move)')
# copy: Downloads/song.mp3 copied into Documents (source stays)
expect(os.path.isfile(f'{root}/Downloads/song.mp3'), 'Downloads/song.mp3 still there (copy)')
expect(os.path.isfile(f'{root}/Documents/song.mp3'), 'Documents/song.mp3 exists (copy)')
# untouched samples
expect(os.path.isfile(f'{root}/Documents/ntos.html'), 'ntos.html intact for the edge hand-off')
expect(os.path.isfile(f'{root}/Documents/readme.txt'), 'readme.txt intact')

if fails:
    print(f'{fails} FS FAILURES')
    sys.exit(1)
print('file operations verified on disk')
