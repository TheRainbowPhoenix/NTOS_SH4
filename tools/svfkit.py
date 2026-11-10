#!/usr/bin/env python3
"""svfkit.py — stdlib-only editor kit for NTOS .svf vector icons.

The SVF format is the phone's VEC command stream (payload/phone/svf.c):
big-endian int16 coordinates in a virtual viewport (default 32x32),
RGB555 pigments, 0xffff = transparent. See tools/svf-format.md.

    python3 tools/svfkit.py dump FILE.svf
    python3 tools/svfkit.py build FILE.svftxt FILE.svf [--vec2]
    python3 tools/svfkit.py preview FILE.svf OUT.png [--size N] [--tint RRGGBB]
    python3 tools/svfkit.py validate FILE.svf ...
    python3 tools/svfkit.py kit OUT.zip

The `kit` command packs every .svf asset in the tree (repo-relative
paths, so edited files drop straight back in) plus this tool and the
format doc — that zip is the CI artifact for hand-editing icons.
"""
import binascii
import os
import re
import struct
import sys
import zipfile

NONE = 0xFFFF
FIXED_DATE = (2025, 1, 1, 0, 0, 0)
ASSET_ROOTS = (
    "payload/phone/assets",
    "edge/app/tests/assets",
    "calc/app/assets",
    "settings/app/assets",
    "files/app/assets",
    "cortana/app/assets",
)
# 32 angular samples (svf.c arc_x/arc_y, offline cos/sin * 1024)
ARC_X = [1024, 1004, 946, 851, 724, 569, 392, 200, 0, -200, -392, -569,
         -724, -851, -946, -1004, -1024, -1004, -946, -851, -724, -569,
         -392, -200, 0, 200, 392, 569, 724, 851, 946, 1004]
ARC_Y = [0, 200, 392, 569, 724, 851, 946, 1004, 1024, 1004, 946, 851,
         724, 569, 392, 200, 0, -200, -392, -569, -724, -851, -946,
         -1004, -1024, -1004, -946, -851, -724, -569, -392, -200]


def u16(b, p):
    return (b[p] << 8) | b[p + 1]


def i16(b, p):
    v = u16(b, p)
    return v - 0x10000 if v > 0x7FFF else v


def rgb555(c):
    """Device ink rules (svf.c ink()): 0x4210 is the 50%-black second
    ink; raw 5-bit channels scale to 8-bit."""
    if c == 0x4210:
        return (127, 127, 127)
    if c == NONE:
        return None
    return ((c >> 10) & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, (c & 31) * 255 // 31


def near_white(c):
    r, g, b = (c >> 10) & 31, (c >> 5) & 31, c & 31
    lo, hi = min(r, g, b), max(r, g, b)
    return lo >= 22 and hi - lo <= 3


def ink_of(c, tint):
    raw = rgb555(c)
    if raw is None:
        return None
    if tint is not None and near_white(c):
        return tint
    return raw


# ---------------------------------------------------------------- parse

class Cmd:
    __slots__ = ("op", "args")

    def __init__(self, op, args):
        self.op, self.args = op, args


def parse(blob):
    """Parse + validate with the device rules (svf.c scan())."""
    n = len(blob)
    if not blob or n < 5 or n > 4096 or blob[:3] != b"VEC" or blob[3] not in (ord("1"), ord("2")):
        raise ValueError("bad header (need VEC1/VEC2, 5..4096 bytes)")
    out, p, count = [], 4, 0
    while p < n and count < 512:
        op = blob[p]
        p += 1
        count += 1
        if op == 0x00:
            if p != n:
                raise ValueError("trailing %d bytes after END" % (n - p))
            return out
        if op in (0x01, 0x60):
            need = 4
        elif op in (0x10, 0x21, 0x30):
            need = 8
        elif op in (0x11, 0x22, 0x31):
            need = 9
        elif op == 0x20:
            need = 6
        elif op == 0x40:
            if p >= n or blob[p] < 3:
                raise ValueError("poly: need >= 3 points")
            need = 1 + 4 * blob[p]
        elif op == 0x55:
            if p >= n:
                raise ValueError("comment: truncated length")
            need = 1 + blob[p]
        elif op == 0x50:
            if n - p < 5:
                raise ValueError("text: truncated")
            need = 5 + blob[p + 4]
        else:
            raise ValueError("unknown op 0x%02x at offset %d" % (op, p - 1))
        if need > n - p:
            raise ValueError("op 0x%02x: truncated (need %d, have %d)" % (op, need, n - p))
        if op in (0x22, 0x31) and (blob[p + 8] == 0 or blob[p + 8] > 8):
            raise ValueError("op 0x%02x: width must be 1..8" % op)
        if op == 0x22:
            if blob[p + 6] >= 32 or blob[p + 7] > 32:
                raise ValueError("arc: start<32, count<=32")
            if i16(blob, p + 4) < 0:
                raise ValueError("arc: negative radius")
        if op == 0x60 and not (1 <= u16(blob, p) <= 4096 and 1 <= u16(blob, p + 2) <= 4096):
            raise ValueError("viewport: 1..4096 only")
        if op == 0x01:
            out.append(Cmd(0x01, [u16(blob, p), u16(blob, p + 2)]))
        elif op == 0x60:
            out.append(Cmd(0x60, [u16(blob, p), u16(blob, p + 2)]))
        elif op == 0x55:
            out.append(Cmd(0x55, [blob[p:p + blob[p]]]))
        elif op == 0x50:
            out.append(Cmd(0x50, [i16(blob, p), i16(blob, p + 2),
                                 blob[p + 5:p + 5 + blob[p + 4]]]))
        elif op == 0x40:
            m = blob[p]
            pts = [(i16(blob, p + 1 + 4 * i), i16(blob, p + 3 + 4 * i)) for i in range(m)]
            out.append(Cmd(0x40, pts))
        elif op == 0x22:
            out.append(Cmd(0x22, [i16(blob, p), i16(blob, p + 2), i16(blob, p + 4),
                                 blob[p + 6], blob[p + 7], blob[p + 8]]))
        elif op == 0x31:
            out.append(Cmd(0x31, [i16(blob, p), i16(blob, p + 2), i16(blob, p + 4),
                                 i16(blob, p + 6), blob[p + 8]]))
        elif op == 0x20:
            out.append(Cmd(0x20, [i16(blob, p), i16(blob, p + 2), i16(blob, p + 4)]))
        else:  # 0x10 0x11 0x21 0x30
            extra = blob[p + 8] if op == 0x11 else None
            out.append(Cmd(op, [i16(blob, p), i16(blob, p + 2), i16(blob, p + 4),
                                i16(blob, p + 6), extra]))
        p += need
    raise ValueError("missing END (or > 512 commands)")


# ---------------------------------------------------------------- dump

def dump(blob, name=""):
    cmds = parse(blob)
    lines = ["# %s (%d bytes, %d commands)" % (name or "svf", len(blob), len(cmds))]
    for c in cmds:
        op = c.op
        if op == 0x00:
            lines.append("end")
        elif op == 0x01:
            lines.append("set fill=%04x stroke=%04x" % (c.args[0], c.args[1]))
        elif op == 0x60:
            lines.append("viewport %d %d" % (c.args[0], c.args[1]))
        elif op == 0x55:
            lines.append("comment %s" % c.args[0].decode("latin1"))
        elif op == 0x50:
            lines.append('text %d %d "%s"' % (c.args[0], c.args[1], c.args[2].decode("latin1")))
        elif op == 0x40:
            lines.append("poly " + " ".join("%d %d" % p for p in c.args))
        elif op == 0x22:
            a = c.args
            lines.append("arc %d %d %d %d %d %d" % tuple(a))
        elif op == 0x31:
            a = c.args
            lines.append("linew %d %d %d %d %d" % tuple(a))
        elif op == 0x20:
            lines.append("circle %d %d %d" % tuple(c.args))
        elif op == 0x11:
            a = c.args
            lines.append("rectw %d %d %d %d %d" % tuple(a))
        else:
            lines.append("%s %d %d %d %d" % ({0x10: "rect", 0x21: "ellipse", 0x30: "line"}[op],
                                             c.args[0], c.args[1], c.args[2], c.args[3]))
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------- build

def parse_pigment(tok):
    if tok in ("none", "ffff", "FFFF", "-"):
        return NONE
    m = re.fullmatch(r"([0-9a-fA-F]{1,4})", tok)
    if not m:
        raise ValueError("bad pigment %r (1-4 hex digits RGB555, or 'none')" % tok)
    v = int(tok, 16)
    if v > 0xFFFE:
        raise ValueError("pigment %s: ffff is 'none' (transparent)" % tok)
    return v  # the device masks to 5 bits per channel; bit 15 is ignored


def build(text, vec2=False):
    body = bytearray(b"VEC2" if vec2 else b"VEC1")
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip() if not raw.lstrip().startswith("#") else ""
        line = raw.strip()
        if line.startswith("#") or not line:
            continue
        try:
            mset = re.fullmatch(r"set\s+fill\s*=\s*(\S+)\s+stroke\s*=\s*(\S+)", line)
            mset2 = re.fullmatch(r"set\s+(\S+)\s+(\S+)", line)
            tok = line.split()
            head = tok[0].lower()
            nums = []
            for t in tok[1:]:
                if t.lstrip("-").isdigit():
                    nums.append(int(t))
            if mset:
                body += bytes([0x01])
                body += struct.pack(">HH", parse_pigment(mset.group(1)), parse_pigment(mset.group(2)))
            elif mset2 and head == "set":
                body += bytes([0x01])
                body += struct.pack(">HH", parse_pigment(mset2.group(1)), parse_pigment(mset2.group(2)))
            elif head == "viewport":
                if len(nums) < 2 or not (1 <= nums[0] <= 4096 and 1 <= nums[1] <= 4096):
                    raise ValueError("viewport w h (1..4096)")
                body += bytes([0x60]) + struct.pack(">HH", nums[0], nums[1])
            elif head == "comment":
                rest = line.split(None, 1)[1] if len(line.split(None, 1)) > 1 else ""
                bb = rest.encode("latin1")[:255]
                body += bytes([0x55, len(bb)]) + bb
            elif head == "text":
                parts = line.split(None, 3)
                if len(nums) < 2 or len(parts) < 4:
                    raise ValueError("text x y \"STRING\"")
                s = parts[3].strip('"').encode("latin1")[:255]
                body += bytes([0x50]) + struct.pack(">hh", nums[0], nums[1]) + bytes([len(s)]) + s
            elif head == "poly":
                rest = tok[1:]
                pts = []
                for i in range(0, len(rest) - 1, 2):
                    if rest[i].lstrip("-").isdigit() and rest[i + 1].lstrip("-").isdigit():
                        pts.append((int(rest[i]), int(rest[i + 1])))
                if not 3 <= len(pts) <= 255:
                    raise ValueError("poly: 3..255 points")
                body += bytes([0x40, len(pts)])
                for x, y in pts:
                    body += struct.pack(">hh", x, y)
            elif head == "arc":
                w = nums[5] if len(nums) >= 6 else 1
                if len(nums) < 5:
                    raise ValueError("arc cx cy r start count [w]")
                if not (0 <= nums[3] < 32 and 1 <= nums[4] <= 32 and 1 <= w <= 8 and nums[2] >= 0):
                    raise ValueError("arc: start<32 count<=32 w<=8 r>=0")
                body += bytes([0x22]) + struct.pack(">hhh", nums[0], nums[1], nums[2])
                body += bytes([nums[3], nums[4], w])
            elif head == "linew":
                w = nums[4] if len(nums) >= 5 else 1
                if len(nums) < 4:
                    raise ValueError("linew x0 y0 x1 y1 [w]")
                if not 1 <= w <= 8:
                    raise ValueError("width 1..8")
                body += bytes([0x31]) + struct.pack(">hhhh", nums[0], nums[1], nums[2], nums[3]) + bytes([w])
            elif head == "circle":
                if len(nums) < 3 or nums[2] < 0:
                    raise ValueError("circle cx cy r")
                body += bytes([0x20]) + struct.pack(">hhh", nums[0], nums[1], nums[2])
            elif head == "rectw":
                if len(nums) < 5 or not 1 <= nums[4] <= 16:
                    raise ValueError("rectw l t r b w(1..16)")
                body += bytes([0x11]) + struct.pack(">hhhh", *nums[:4]) + bytes([nums[4]])
            elif head == "rect":
                if len(nums) < 4:
                    raise ValueError("rect l t r b")
                body += bytes([0x10]) + struct.pack(">hhhh", *nums[:4])
            elif head == "ellipse":
                if len(nums) < 4:
                    raise ValueError("ellipse l t r b")
                body += bytes([0x21]) + struct.pack(">hhhh", *nums[:4])
            elif head == "line":
                if len(nums) < 4:
                    raise ValueError("line x0 y0 x1 y1")
                body += bytes([0x30]) + struct.pack(">hhhh", *nums[:4])
            elif head == "end":
                break
            else:
                raise ValueError("unknown command %r" % head)
        except (IndexError, ValueError) as e:
            raise ValueError("line %d: %s" % (lineno, e))
    body += bytes([0x00])
    blob = bytes(body)
    parse(blob)  # validate with the device rules before shipping
    return blob


# ------------------------------------------------------------- preview

class Canvas:
    def __init__(self, size):
        self.size = size
        self.px = bytearray(b"\xff" * (size * size * 3))  # white

    def put(self, x, y, rgb):
        if rgb is None:
            return
        if 0 <= x < self.size and 0 <= y < self.size:
            o = (y * self.size + x) * 3
            self.px[o:o + 3] = bytes(rgb)

    def fill_rect(self, l, t, r, b, rgb):
        if rgb is None:
            return
        l, t = max(l, 0), max(t, 0)
        r, b = min(r, self.size), min(b, self.size)
        if r <= l or b <= t:
            return
        row = bytes(rgb) * (r - l)
        for y in range(t, b):
            o = (y * self.size + l) * 3
            self.px[o:o + (r - l) * 3] = row

    def seg(self, x0, y0, x1, y1, rgb):
        if rgb is None:
            return
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.put(x0, y0, rgb)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def stroke_seg(self, x0, y0, x1, y1, width, rgb):
        """Width as parallel offsets across the shortest axis (svf.c)."""
        begin = -((width - 1) // 2)
        for i in range(width):
            off = begin + i
            if abs(x1 - x0) >= abs(y1 - y0):
                self.seg(x0, y0 + off, x1, y1 + off, rgb)
            else:
                self.seg(x0 + off, y0, x1 + off, y1, rgb)


def render(blob, size, tint=None):
    cmds = parse(blob)
    cv = Canvas(size)
    fill = stroke = NONE
    vw = vh = 32

    def CX(v):
        return (v * size) // vw

    def CY(v):
        return (v * size) // vh

    for c in cmds:
        op = c.op
        if op == 0x00:
            break
        if op == 0x01:
            fill, stroke = c.args
        elif op == 0x60:
            vw, vh = c.args
        elif op == 0x55:
            pass
        elif op == 0x50:
            x, y, s = c.args
            if stroke != NONE:
                # the device renders 12px phone-font text; the preview
                # marks the run as a grey bar (no shipped asset uses it)
                cv.fill_rect(CX(x), CY(y), CX(x) + max(7 * len(s), 14),
                             CY(y) + max(size // 8, 4), (170, 170, 170))
        elif op == 0x40:
            pts = [(CX(x), CY(y)) for x, y in c.args]
            fi, so = ink_of(fill, tint), ink_of(stroke, tint)
            if fi is not None and len(pts) >= 3:  # even-odd scanline fill
                ys = [p[1] for p in pts]
                for y in range(min(ys), max(ys) + 1):
                    xs = []
                    for i in range(len(pts)):
                        x0, y0 = pts[i]
                        x1, y1 = pts[(i + 1) % len(pts)]
                        if (y0 <= y < y1) or (y1 <= y < y0):
                            xs.append(x0 + (x1 - x0) * (y - y0) // (y1 - y0))
                    xs.sort()
                    for k in range(0, len(xs) - 1, 2):
                        cv.fill_rect(xs[k], y, xs[k + 1] + 1, y + 1, fi)
            if so is not None:
                for i in range(len(pts)):
                    cv.seg(pts[i][0], pts[i][1], pts[(i + 1) % len(pts)][0],
                           pts[(i + 1) % len(pts)][1], so)
        elif op == 0x22:
            cx, cy, r, start, count, w = c.args
            so = ink_of(stroke, tint)
            if so is not None and count:
                px = CX(cx + (r * ARC_X[start]) // 1024)
                py = CY(cy + (r * ARC_Y[start]) // 1024)
                for i in range(1, count + 1):
                    st = (start + i) & 31
                    nx = CX(cx + (r * ARC_X[st]) // 1024)
                    ny = CY(cy + (r * ARC_Y[st]) // 1024)
                    cv.stroke_seg(px, py, nx, ny, max(1, w * size // vw), so)
                    px, py = nx, ny
        elif op == 0x31:
            x0, y0, x1, y1, w = c.args
            so = ink_of(stroke, tint)
            if so is not None:
                cv.stroke_seg(CX(x0), CY(y0), CX(x1), CY(y1),
                              max(1, w * size // vw), so)
        elif op == 0x20:
            # device: fill draws the disk, stroke draws the 32-segment
            # ring (r->ellipse ...) - never a stroke-coloured disk
            cx, cy, r = c.args
            fi, so = ink_of(fill, tint), ink_of(stroke, tint)
            cxs, cys, rr = CX(cx), CY(cy), max(1, (r * size) // max(vw, 1))
            if fi is not None:
                for y in range(cys - rr - 1, cys + rr + 2):
                    for x in range(cxs - rr - 1, cxs + rr + 2):
                        if (x - cxs) ** 2 + (y - cys) ** 2 <= rr * rr:
                            cv.put(x, y, fi)
            if so is not None:
                prev = None
                for i in range(33):
                    st = i & 31
                    x = cxs + (rr * ARC_X[st]) // 1024
                    y = cys + (rr * ARC_Y[st]) // 1024
                    if prev:
                        cv.seg(prev[0], prev[1], x, y, so)
                    prev = (x, y)
        elif op == 0x21:
            l, t, r, b = c.args[:4]
            fi, so = ink_of(fill, tint), ink_of(stroke, tint)
            ecx, ecy = (CX(l) + CX(r)) // 2, (CY(t) + CY(b)) // 2
            rx, ry = max(1, (CX(r) - CX(l)) // 2), max(1, (CY(b) - CY(t)) // 2)
            if fi is not None:
                for y in range(ecy - ry - 1, ecy + ry + 2):
                    for x in range(ecx - rx - 1, ecx + rx + 2):
                        if ((x - ecx) / rx) ** 2 + ((y - ecy) / ry) ** 2 <= 1.0:
                            cv.put(x, y, fi)
            if so is not None:
                prev = None
                for i in range(33):
                    st = i & 31
                    x = ecx + (rx * ARC_X[st]) // 1024
                    y = ecy + (ry * ARC_Y[st]) // 1024
                    if prev:
                        cv.seg(prev[0], prev[1], x, y, so)
                    prev = (x, y)
        else:
            l, t, r, b = c.args[:4]
            bw = c.args[4] if op == 0x11 else 1
            fi, so = ink_of(fill, tint), ink_of(stroke, tint)
            L, T, R, B = CX(l), CY(t), CX(r), CY(b)
            if op == 0x30:
                cv.seg(L, T, R, B, so)
            else:
                cv.fill_rect(L, T, R, B, fi)
                if so is not None:
                    w = max(1, bw * size // vw)
                    cv.fill_rect(L, T, R, T + w, so)
                    cv.fill_rect(L, B - w, R, B, so)
                    cv.fill_rect(L, T, L + w, B, so)
                    cv.fill_rect(R - w, T, R, B, so)
    return cv


def png_bytes(cv):
    import zlib
    raw = bytearray()
    stride = cv.size * 3
    for y in range(cv.size):
        raw.append(0)
        raw += cv.px[y * stride:(y + 1) * stride]

    def chunk(typ, data):
        return len(data).to_bytes(4, "big") + typ + data + binascii.crc32(typ + data).to_bytes(4, "big")

    def chunk_zlib(typ, data):
        c = zlib.compress(data, 9)
        return chunk(typ, c)

    ihdr = struct.pack(">IIBBBBB", cv.size, cv.size, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk_zlib(b"IDAT", bytes(raw))
            + chunk(b"IEND", b""))


def write_png(path, cv):
    data = png_bytes(cv)
    with open(path, "wb") as f:
        f.write(data)
    return len(data)


def preview_nonblank(cv):
    px = cv.px
    first = bytes(px[:3])
    for i in range(0, len(px), 997 * 3):
        if px[i:i + 3] != first:
            return True
    for i in range(0, len(px), 3):
        if px[i:i + 3] != first:
            return True
    return False


# ---------------------------------------------------------------- kit

def kit_zip(out):
    names = ["tools/svfkit.py", "tools/svf-format.md"]
    for root in ASSET_ROOTS:
        if not os.path.isdir(root):
            continue
        for dp, dns, fns in os.walk(root):
            dns.sort()
            for fn in sorted(fns):
                if fn.endswith(".svf"):
                    names.append(os.path.relpath(os.path.join(dp, fn), ".").replace(os.sep, "/"))
    names = sorted(set(names))
    svfs = [n for n in names if n.endswith(".svf")]
    readme = (
        "NTOS SVF editing kit\n"
        "====================\n\n"
        "Every .svf icon in the tree, at its repo-relative path — edit a\n"
        "file, commit it at the same path, push, and CI rebuilds the apps\n"
        "with your art. tools/svfkit.py (python3 stdlib only) is the tool\n"
        "and tools/svf-format.md is the spec:\n\n"
        "  python3 tools/svfkit.py dump payload/phone/assets/settings/battery.svf\n"
        "  python3 tools/svfkit.py preview payload/phone/assets/settings/battery.svf out.png --size 128 --tint 000000\n"
        "  python3 tools/svfkit.py build my-icon.txt my-icon.svf\n\n"
        "The hand-edit loop: unzip over the repo root, dump the icon you\n"
        "want, tweak the text, build, preview, then commit the .svf.\n\n"
        "Icons in this zip:\n" + "\n".join("  " + n for n in svfs) + "\n"
    )
    with zipfile.ZipFile(out, "w") as z:
        def put(name, data):
            zi = zipfile.ZipInfo(name, date_time=FIXED_DATE)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            z.writestr(zi, data)
        put("README.txt", readme.encode())
        for n in names:
            with open(n, "rb") as f:
                blob = f.read()
            if n.endswith(".svf"):
                parse(blob)  # every packed asset must validate
            put(n, blob)
    return out, len(names) + 1


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "dump" and len(argv) == 3:
        with open(argv[2], "rb") as f:
            sys.stdout.write(dump(f.read(), argv[2]))
        return 0
    if cmd == "validate" and len(argv) >= 3:
        bad = 0
        for path in argv[2:]:
            try:
                with open(path, "rb") as f:
                    parse(f.read())
                print("ok   %s" % path)
            except ValueError as e:
                print("FAIL %s: %s" % (path, e))
                bad += 1
        return 1 if bad else 0
    if cmd == "build" and len(argv) in (4, 5):
        with open(argv[2], "r") as f:
            text = f.read()
        blob = build(text, vec2="--vec2" in argv)
        with open(argv[3], "wb") as f:
            f.write(blob)
        print("built %s (%d bytes, validates)" % (argv[3], len(blob)))
        return 0
    if cmd == "preview" and len(argv) >= 4:
        tint = None
        size = 128
        args = argv[4:]
        for i, a in enumerate(args):
            if a == "--tint" and i + 1 < len(args):
                h = args[i + 1]
                tint = tuple(int(h[k:k + 2], 16) for k in (0, 2, 4))
            elif a == "--size" and i + 1 < len(args):
                size = int(args[i + 1])
        with open(argv[2], "rb") as f:
            cv = render(f.read(), size, tint)
        n = write_png(argv[3], cv)
        ok = preview_nonblank(cv)
        print("wrote %s (%d bytes)%s" % (argv[3], n, "" if ok else " (WARNING: blank image)"))
        return 0 if ok else 1
    if cmd == "kit" and len(argv) == 3:
        out, count = kit_zip(argv[2])
        print("kit %s (%d entries)" % (out, count))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
