#!/usr/bin/env python3
"""pri_dump.py — parse MRT v2 resources.pri (mrm_pri2) from the Edge UWP package.

Reverses the on-disk layout:
  header   : magic 'mrm_pri2', version, filesize, flags, hdrlen, checksum
  directory: 32-byte entries { char name[16]; u64 unk; u32 offset; u32 size }
             starting at 0x20, count = (hdrlen-0x20)/32
  sections : [mrm_decn_info] [mrm_pridescex] [mrm_hschemaex] [mrm_res_map2_]
             and N x [mrm_dataitem] (one per language blob, mostly)

Modes:
  default : print section directory + carve all embedded files (XBF/PNG/...)
  --map   : dump resource URI strings found in the resource map section
Files are carved to edge/analysis/pri_out/<...>
"""
import sys, os, struct, re

OUTROOT_DEFAULT = os.path.join(os.path.dirname(__file__), "..", "analysis", "pri_out")

MAGIC = b"mrm_pri2"


def parse_header(data):
    assert data[:8] == MAGIC, "not a mrm_pri2 file"
    version, filesize, flags, hdrlen, checksum = struct.unpack_from("<5I", data, 8)
    return version, filesize, flags, hdrlen, checksum


def parse_dir(data, hdrlen):
    entries = []
    off = 0x20
    while off + 32 <= hdrlen:
        name = data[off:off + 16].rstrip(b"\0 ").decode(errors="replace")
        unk = struct.unpack_from("<Q", data, off + 16)[0]
        sec_off, sec_size = struct.unpack_from("<II", data, off + 24)
        entries.append((name, unk, sec_off, sec_size))
        off += 32
    return entries


def carve_strings(data, blob, base):
    """extract UTF-16LE printable strings >= 4 chars"""
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){4,}", blob):
        s = m.group().decode("utf-16-le", errors="replace")
        yield base + m.start(), s


FILE_MAGICS = [
    (b"XBF2", "xbf"),
    (b"\x89PNG\r\n\x1a\n", "png"),
    (b"GIF8", "gif"),
    (b"\xff\xd8\xff", "jpg"),
    (b"RIFF", "wav"),
    (b"OggS", "ogg"),
]


def carve_files(data, blob, base, outdir, tag):
    """carve known file magics; XBF length must be derived from structure."""
    found = []
    for magic, ext in FILE_MAGICS:
        start = 0
        while True:
            i = blob.find(magic, start)
            if i < 0:
                break
            found.append((base + i, ext))
            start = i + 1
    return found


def xbf_size(buf, off):
    """XBF2 header: 'XBF2' u32 ver u32 flags? then string heap offsets...
    We cannot know size from header alone; use next magic or section end."""
    return None


def main():
    path = sys.argv[1]
    mode = "carve"
    args = sys.argv[2:]
    outdir = OUTROOT_DEFAULT
    if "--map" in args:
        mode = "map"
    if "--out" in args:
        outdir = args[args.index("--out") + 1]
    os.makedirs(outdir, exist_ok=True)
    data = open(path, "rb").read()
    version, filesize, flags, hdrlen, checksum = parse_header(data)
    print(f"magic=mrm_pri2 version={version:#x} filesize={filesize} actual={len(data)} "
          f"flags={flags:#x} hdrlen={hdrlen:#x} checksum={checksum:#x}")
    entries = parse_dir(data, hdrlen)
    print(f"{len(entries)} sections:")
    counts = {}
    for name, unk, off, size in entries:
        counts[name] = counts.get(name, 0) + 1
        if counts[name] == 1:
            print(f"  [{name}] first@{off:#x} size={size:#x}")
    for name, n in sorted(counts.items()):
        print(f"  total {name}: {n}")

    if mode == "map":
        # dump all resource-map-ish strings from res_map2 section
        for name, unk, off, size in entries:
            if "res_map" in name or "hschema" in name:
                blob = data[off:off + size]
                out = os.path.join(outdir, name.strip("[]") + ".strings.txt")
                with open(out, "w") as f:
                    seen = set()
                    for addr, s in carve_strings(data, blob, off):
                        if s not in seen:
                            seen.add(s)
                            f.write(f"{addr:#010x} {s}\n")
                print(f"wrote {out}")
        return

    # ---- carve mode: extract every embedded payload blob from dataitem sections
    idx = 0
    manifest = []
    for name, unk, off, size in entries:
        if "dataitem" not in name:
            continue
        idx += 1
        blob = data[off:off + size]
        # find XBF2 blobs and carve with heuristics: they end at next magic or
        # section end. XBF2 parsing (xbf_dump.py) trims the real tree anyway.
        pos = 0
        while True:
            i = blob.find(b"XBF2", pos)
            if i < 0:
                break
            # XBF2: magic(4) ver(4) ... at +0x0c is u32 totalSize? try both.
            sz = None
            if i + 0x20 <= len(blob):
                # heuristics: scan u32s for a size that lands near next XBF2/PNG
                nxt = len(blob)
                for m2 in (b"XBF2", b"\x89PNG"):
                    j = blob.find(m2, i + 4)
                    if j > 0:
                        nxt = min(nxt, j)
                sz = nxt - i
            fn = os.path.join(outdir, f"dataitem{idx:03d}_{i:07x}.xbf")
            with open(fn, "wb") as f:
                f.write(blob[i:i + sz])
            manifest.append((fn, size, sz))
            pos = i + 4
        # PNG carve
        pos = 0
        while True:
            i = blob.find(b"\x89PNG", pos)
            if i < 0:
                break
            from struct import unpack_from as u
            if i + 8 <= len(blob):
                pngsz = u(">I", blob, i + 4)[0] + 12  # IHDR len field approx end
                # better: walk chunks
                p = i + 8
                try:
                    while p + 8 <= len(blob):
                        ln = u(">I", blob, p)[0]
                        p += 12 + ln
                        if blob[p - 4:p] == b"IEND":
                            break
                    pngsz = p - i
                except Exception:
                    pass
                fn = os.path.join(outdir, f"dataitem{idx:03d}_{i:07x}.png")
                with open(fn, "wb") as f:
                    f.write(blob[i:i + pngsz])
            pos = i + 4
    print(f"carved {len(manifest)}+ blobs to {outdir}")
    # also dump all strings of the first dataitem for names
    for name, unk, off, size in entries:
        if "dataitem" in name:
            blob = data[off:off + size]
            out = os.path.join(outdir, "dataitem_all.strings.txt")
            with open(out, "w") as f:
                for addr, s in carve_strings(data, blob, off):
                    f.write(f"{addr:#010x} {s}\n")
            print(f"wrote {out}")
            break


if __name__ == "__main__":
    main()
