#!/usr/bin/env python3
"""make_tree.py — build the sample \\fls0\\ tree used by the files e2e.

Usage: python3 make_tree.py <fls0-dir>
Creates the W10M Files-like layout (This device quick folders + samples).
"""
import os, sys, time

root = sys.argv[1]
folders = [
    "Documents", "Documents/Notes", "Pictures", "Downloads",
    "Music", "Videos",
]
files = {
    "Documents/readme.txt": b"File Explorer POC sample file.\n"
                            b"Ported from Microsoft.Windows.FileExplorer (W10M).\n",
    "Documents/Notes/todo.txt": b"1. port the XBF pages\n2. wire the keyboard\n3. ship\n",
    "Documents/report.pdf": b"%PDF-1.4 fake sample for the icon grid\n",
    "Documents/ntos.html": b"<html><head><title>NTOS File Explorer</title></head>"
                           b"<body><h1>Hello from files</h1>"
                           b"<p>This local page was opened from File Explorer "
                           b"through a <b>file://</b> activation of Edge.</p>"
                           b"<ul><li>files app taps an .html item</li>"
                           b"<li>CreateProcessW hands the URL to edge</li>"
                           b"<li>edge renders it with the md engine</li></ul>"
                           b"</body></html>\n",
    "Pictures/photo_001.jpg": b"\xff\xd8\xff\xe0 JPEG sample",
    "Pictures/wallpaper.png": b"\x89PNG\r\n\x1a\n sample",
    "Downloads/setup.cab": b"MSCF sample cabinet",
    "Downloads/song.mp3": b"ID3 sample audio",
    "Music/track01.mp3": b"ID3 another sample",
    "Videos/clip.mp4": b"\x00\x00\x00\x18ftypmp42 sample",
    "data.dat": b"0123456789" * 103,  # 1030 bytes -> "1.0 KB"
}
# bulk rows so Downloads overflows the 396px list area (drag-scroll e2e);
# "zz_" keeps them sorted behind the named samples
for i in range(16):
    files[f"Downloads/zz_bulk_{i:02d}.dat"] = b"bulk sample %02d" % i
for f in folders:
    os.makedirs(os.path.join(root, f), exist_ok=True)
for path, data in files.items():
    with open(os.path.join(root, path), "wb") as fh:
        fh.write(data)
# deterministic mtimes: stagger by entry for sort-by-date tests
t0 = 1770000000
for i, path in enumerate(files):
    os.utime(os.path.join(root, path), (t0 + i * 3600, t0 + i * 3600))
print(f"sample tree ready under {root}")
