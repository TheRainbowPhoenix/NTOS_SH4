#!/usr/bin/env python3
"""pri_decode.py — full mrm_pri2 decoder (layout per chausner/PriTools RE).

Decodes:
  header + TOC + sections (identifier/qualifier/flags/length + DEF5FADE footer)
  rm_decn_info  : decisions -> qualifier sets -> qualifiers (Language, Scale...)
  rm_hschema(ex): scopes/items tree (ascii + unicode name blocks)
  rm_res_map2_  : item -> iteminfo -> candidates
                  type 0 = inline string (len, offset into string data area)
                  type 1 = dataitem reference (span into a dataitem section)
  rm_dataitem   : string/blob span tables + data area

Output: for every resource item (full path) every candidate with its
qualifier set (language etc.) and decoded value. Values are UTF-16 when the
resource type is String/Path, dumped as hex otherwise.

Usage: python3 pri_decode.py <resources.pri> [--strings] [--out DIR]
"""
import sys, os, struct

RVT = {0: "String", 1: "Path", 2: "EmbeddedData", 3: "AsciiString",
       4: "Utf8String", 5: "AsciiPath", 6: "Utf8Path"}
QT = {0: "Language", 1: "Contrast", 2: "Scale", 3: "HomeRegion", 4: "TargetSize",
      5: "LayoutDirection", 6: "Theme", 7: "AlternateForm", 8: "DXFeatureLevel",
      9: "Configuration", 10: "DeviceFamily", 11: "Custom"}


class R:
    def __init__(self, data, pos):
        self.d, self.p = data, pos

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v

    def chars(self, n):
        v = self.d[self.p:self.p + n].decode("latin1"); self.p += n; return v

    def wstr(self, maxlen=None):
        out = []
        while True:
            c = struct.unpack_from("<H", self.d, self.p)[0]
            self.p += 2
            if c == 0:
                break
            out.append(chr(c))
            if maxlen and len(out) >= maxlen:
                break
        return "".join(out)


def align8(x):
    return (x + 7) & ~7


def parse_header(data):
    assert data[:8] == b"mrm_pri2", "not mrm_pri2"
    r = R(data, 8)
    assert r.u16() == 0
    assert r.u16() == 1
    total = r.u32()
    toc_off = r.u32()
    sec_start = r.u32()
    nsec = r.u16()
    assert r.u16() == 0xFFFF
    assert r.u32() == 0
    # footer check
    assert struct.unpack_from("<I", data, total - 16)[0] == 0xDEFFFADE
    return total, toc_off, sec_start, nsec


def parse_toc(data, toc_off, nsec):
    r = R(data, toc_off)
    toc = []
    for _ in range(nsec):
        name = r.chars(16)
        flags, sflags = r.u16(), r.u16()
        qual, off, length = r.u32(), r.u32(), r.u32()
        toc.append(dict(name=name, flags=flags, sflags=sflags, qual=qual,
                        off=off, length=length))
    return toc


def section_content(data, sec_start, e):
    """returns (content_bytes, content_file_offset, content_length)"""
    base = sec_start + e["off"]
    r = R(data, base)
    name = r.chars(16)
    assert name == e["name"], f"{name!r} != {e['name']!r}"
    qual = r.u32()
    flags, sflags = r.u16(), r.u16()
    length = r.u32()
    assert r.u32() == 0
    assert length == e["length"], (hex(length), hex(e["length"]))
    clen = length - 32 - 8
    # footer check
    foff = r.p + clen
    assert struct.unpack_from("<I", data, foff)[0] == 0xDEF5FADE, hex(foff)
    assert struct.unpack_from("<I", data, foff + 4)[0] == length
    return data[r.p:r.p + clen], r.p, clen


def parse_decn(data, sec_start, e):
    c, cpos, clen = section_content(data, sec_start, e)
    r = R(c, 0)
    ndq, nq, nqs, nd, nidx, tdl = (r.u16() for _ in range(6))
    decisions = []
    for _ in range(nd):
        decisions.append((r.u16(), r.u16()))  # firstQSI, nQS
    qsets = []
    for _ in range(nqs):
        qsets.append((r.u16(), r.u16()))  # firstQI, nQ
    quals = []
    for _ in range(nq):
        idx, prio, fb = r.u16(), r.u16(), r.u16()
        assert r.u16() == 0
        quals.append((idx, prio, fb))
    dqinfos = []
    for _ in range(ndq):
        atom, qtype, op, vtype = r.u16(), r.u16(), r.u16(), r.u16()
        voff = r.u32()
        dqinfos.append((atom, qtype, op, vtype, voff))
    index_table = [r.u16() for _ in range(nidx)]
    data_start = r.p
    qualifiers = []
    for i in range(nq):
        idx, prio, fb = quals[i]
        atom, qtype, op, vtype, voff = dqinfos[idx]
        q = R(c, data_start + voff * 2)
        val = q.wstr()
        qualifiers.append(dict(atom=atom, type=qtype, value=val,
                               prio=prio, fb=fb / 1000.0))
    qset_objs = []
    for first, n in qsets:
        qset_objs.append([qualifiers[index_table[first + j]] for j in range(n)])
    dec_objs = []
    for first, n in decisions:
        dec_objs.append([qset_objs[index_table[first + j]] for j in range(n)])
    return dec_objs


def parse_hschema(data, sec_start, e):
    c, cpos, clen = section_content(data, sec_start, e)
    r = R(c, 0)
    assert r.u16() == 1
    uname_len, name_len = r.u16(), r.u16()
    assert r.u16() == 0
    ext = e["name"].startswith("[mrm_hschemaex")
    if ext:
        tag = r.chars(16)
        assert tag in ("[def_hnamesx]  \0", "[def_hnames]   \0"), repr(tag)
        extended = tag.startswith("[def_hnamesx")
    else:
        extended = False
    major, minor = r.u16(), r.u16()
    assert r.u32() == 0
    checksum = r.u32()
    nscopes, nitems = r.u32(), r.u32()
    uname = r.wstr(uname_len)
    name = r.wstr(name_len)
    assert r.u16() == 0
    hnames_off = r.p
    r.u16()  # maxFullPathLength
    assert r.u16() == 0
    assert r.u32() == nscopes + nitems
    assert r.u32() == nscopes
    assert r.u32() == nitems
    udl = r.u32()
    thl = r.u32()
    adl = r.u32() if extended else 0
    sai = []
    for _ in range(nscopes + nitems):
        parent, fpl = r.u16(), r.u16()
        first_char = r.u16()
        nl = r.u8()
        flags = r.u8()
        noff = r.u16() | ((flags & 0xF) << 16)
        index = r.u16()
        sai.append(dict(parent=parent, fpl=fpl, nl=nl, flags=flags,
                        noff=noff, index=index))
    # scopes ex
    for _ in range(nscopes):
        r.u16(); r.u16(); r.u16()
        assert r.u16() == 0
    # item index property
    item_prop = [r.u16() for _ in range(nitems)]
    udata_off = r.p
    adata_off = udata_off + udl * 2
    scopes = [None] * nscopes
    items = [None] * nitems
    for i, s in enumerate(sai):
        if s["flags"] & 0x20:  # ascii
            pos = adata_off + s["noff"]
            end = c.index(b"\0", pos)
            nm = c[pos:end].decode("ascii")
        else:
            pos = udata_off + s["noff"] * 2
            q = R(c, pos)
            nm = q.wstr() if s["fpl"] else ""
        idx = s["index"]
        if s["flags"] & 0x10:
            scopes[idx] = dict(index=idx, name=nm, parent=None, children=[])
        else:
            items[idx] = dict(index=idx, name=nm, parent=None)
    # parents
    for s in sai:
        idx = s["index"]
        pnode = sai[s["parent"]]
        pidx = pnode["index"]
        if pidx != 0xFFFF:
            if s["flags"] & 0x10:
                if pidx != idx:  # self-reference = root, keep None
                    scopes[idx]["parent"] = pidx
            else:
                items[idx]["parent"] = pidx
    # children
    for i, s in enumerate(sai[:nscopes + nitems]):
        pass
    # children via scope ex re-walk: simpler — derive from parents
    for it in items:
        if it is not None and it["parent"] is not None:
            scopes[it["parent"]]["children"].append(("item", it["index"]))
    for sc in scopes:
        if sc is not None and sc["parent"] is not None:
            scopes[sc["parent"]]["children"].append(("scope", sc["index"]))
    return scopes, items


def fullname(scopes, items, node, is_scope):
    parts = []
    while True:
        parts.append(node["name"])
        p = node["parent"]
        if p is None:
            break
        node = scopes[p]
    return "\\".join(reversed(parts))


def parse_resmap(data, sec_start, e, toc):
    c, cpos, clen = section_content(data, sec_start, e)
    r = R(c, 0)
    env_len, n_env = r.u16(), r.u16()
    assert env_len == 0 and n_env == 0  # v2
    schema_sec = r.u16()
    hsref_len = r.u16()
    decn_sec = r.u16()
    rvt_size = r.u16()
    n_i2iig = r.u16()
    n_iig = r.u16()
    n_ii = r.u32()
    n_cand = r.u32()
    data_len = r.u32()
    large_len = r.u32()
    r.chars(env_len)
    r.chars(hsref_len)
    rvt = []
    for _ in range(rvt_size):
        assert r.u32() == 4
        rvt.append(r.u32())
    i2iig = [(r.u16(), r.u16()) for _ in range(n_i2iig)]
    iig = [(r.u16(), r.u16()) for _ in range(n_iig)]
    ii = [(r.u16(), r.u16()) for _ in range(n_ii)]
    large = c[r.p:r.p + large_len]
    r.p += large_len
    if large_len:
        q = R(large, 0)
        n_i2iigL, n_iigL, n_iiL = q.u32(), q.u32(), q.u32()
        for _ in range(n_i2iigL):
            i2iig.append((q.u32(), q.u32()))
        for _ in range(n_iigL):
            iig.append((q.u32(), q.u32()))
        for _ in range(n_iiL):
            ii.append((q.u32(), q.u32()))
    cands = []
    for _ in range(n_cand):
        t = r.u8()
        if t == 1:
            rvt_i = r.u8()
            src_file = r.u16()
            di_index = r.u16()
            di_sec = r.u16()
            cands.append(dict(t=1, rvt=rvt[rvt_i], src=src_file,
                              di=di_index, disec=di_sec))
        elif t == 0:
            rvt_i = r.u8()
            ln = r.u16()
            off = r.u32()
            cands.append(dict(t=0, rvt=rvt[rvt_i], ln=ln, off=off))
        else:
            raise ValueError(f"candidate type {t}")
    strdata_start = r.p
    return dict(schema_sec=schema_sec, decn_sec=decn_sec, i2iig=i2iig, iig=iig,
                ii=ii, cands=cands, strdata=c[strdata_start:strdata_start + data_len],
                strdata_foff=cpos + strdata_start)


def parse_dataitem(data, sec_start, e):
    c, cpos, clen = section_content(data, sec_start, e)
    r = R(c, 0)
    assert r.u32() == 0
    nstr, nblob = r.u16(), r.u16()
    total = r.u32()
    data_start = r.p + nstr * 4 + nblob * 8
    spans = []
    for _ in range(nstr):
        off, ln = r.u16(), r.u16()
        spans.append((data_start + off, ln, "str"))
    for _ in range(nblob):
        off, ln = r.u32(), r.u32()
        spans.append((data_start + off, ln, "blob"))
    return c, cpos, spans, data_start, total


def main():
    path = sys.argv[1]
    args = sys.argv[2:]
    data = open(path, "rb").read()
    total, toc_off, sec_start, nsec = parse_header(data)
    toc = parse_toc(data, toc_off, nsec)
    print(f"mrm_pri2 filesize={total} sections={nsec} secStart={sec_start:#x}")

    decn_secs = {}
    hs_secs = {}
    rm_secs = {}
    di_secs = {}
    for i, e in enumerate(toc):
        n = e["name"].strip("\0 []")
        if n.startswith("mrm_decn_info"):
            decn_secs[i] = parse_decn(data, sec_start, e)
        elif n.startswith("mrm_hschema"):
            hs_secs[i] = parse_hschema(data, sec_start, e)
        elif n.startswith("mrm_res_map2"):
            rm_secs[i] = parse_resmap(data, sec_start, e, toc)
        elif n.startswith("mrm_dataitem"):
            di_secs[i] = parse_dataitem(data, sec_start, e)
    print(f"parsed: {len(decn_secs)} decn, {len(hs_secs)} hschema, "
          f"{len(rm_secs)} res_map2, {len(di_secs)} dataitem sections")

    out_lines = []
    for rmi, rm in rm_secs.items():
        scopes, items = hs_secs[rm["schema_sec"]]
        decs = decn_secs[rm["decn_sec"]]
        # walk item->info groups
        for first_item, group in rm["i2iig"]:
            if group < len(rm["iig"]):
                gsize, gfirst = rm["iig"][group]
            else:
                gsize, gfirst = 1, group - len(rm["iig"])
            for k in range(gsize):
                ii_idx = gfirst + k
                if ii_idx >= len(rm["ii"]):
                    continue
                decision, first_cand = rm["ii"][ii_idx]
                item_idx = first_item + k
                if item_idx >= len(items) or items[item_idx] is None:
                    continue
                it = items[item_idx]
                fn = fullname(scopes, items, it, False)
                dec = decs[decision] if decision < len(decs) else []
                ncand = len(dec)
                vals = []
                for ci in range(ncand):
                    cand_idx = first_cand + ci
                    if cand_idx >= len(rm["cands"]):
                        continue
                    cand = rm["cands"][cand_idx]
                    qs = dec[ci] if ci < len(dec) else []
                    langs = "+".join(f"{QT.get(q['type'],q['type'])}={q['value']}" for q in qs)
                    if cand["t"] == 0:
                        raw = rm["strdata"][cand["off"]:cand["off"] + cand["ln"]]
                    else:
                        if cand["disec"] not in di_secs:
                            vals.append((langs, f"<dataitem sec {cand['disec']} # {cand['di']} MISSING>"))
                            continue
                        c, cpos, spans, ds, tot = di_secs[cand["disec"]]
                        if cand["di"] >= len(spans):
                            vals.append((langs, f"<span # {cand['di']} OOR>"))
                            continue
                        off, ln, kind = spans[cand["di"]]
                        raw = c[off:off + ln]
                    if cand["rvt"] in (0, 1):  # String / Path (UTF-16)
                        try:
                            v = raw.decode("utf-16-le").rstrip("\0")
                        except Exception:
                            v = repr(raw)
                    elif cand["rvt"] in (3, 4, 5, 6):  # Ascii/Utf8 string or path
                        v = raw.split(b"\0")[0].decode("utf-8", errors="replace")
                    else:
                        v = f"[{RVT.get(cand['rvt'], cand['rvt'])} {len(raw)}b] {raw[:24].hex()}"
                    vals.append((langs, v))
                out_lines.append((fn, vals))

    outdir = os.path.join(os.path.dirname(__file__), "..", "analysis")
    if "--out" in args:
        outdir = args[args.index("--out") + 1]
    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, "pri_resources.txt")
    with open(out, "w") as f:
        for fn, vals in sorted(out_lines):
            for langs, v in vals:
                v = v.replace("\n", "\\n").replace("\r", "")
                f.write(f"{fn}\t{langs}\t{v}\n")
    print(f"{len(out_lines)} resources -> {out}")

    # en-US dump (single line per resource, the value used for the port)
    en = os.path.join(outdir, "pri_en_us.txt")
    n = 0
    with open(en, "w") as f:
        for fn, vals in sorted(out_lines):
            pick = None
            for langs, v in vals:
                if "Language=EN-US" in langs:
                    pick = v
                    break
                if langs == "":
                    pick = v  # language-neutral fallback
            if pick is not None:
                pick = pick.replace("\n", "\\n").replace("\r", "")
                f.write(f"{fn}\t{pick}\n")
                n += 1
    print(f"{n} en-US values -> {en}")


if __name__ == "__main__":
    main()
