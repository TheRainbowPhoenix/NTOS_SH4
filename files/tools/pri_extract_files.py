#!/usr/bin/env python3
"""pri_extract_files.py — extract every \\Files\\* blob (XBF pages, fonts,
PNG assets) from a W10M resources.pri into a directory.

Reuses the pri_decode parser. Output: <outdir>/<resource-path> with the raw
bytes (XBF pages land as .xbf, ready for xbf_dump.py).
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
import pri_decode as P


def main():
    path = sys.argv[1]
    outdir = sys.argv[2] if len(sys.argv) > 2 else "files_out"
    data = open(path, "rb").read()
    total, toc_off, sec_start, nsec = P.parse_header(data)
    toc = P.parse_toc(data, toc_off, nsec)
    rm = hs = dn = di_any = None
    dis = {}
    for i, e in enumerate(toc):
        n = e["name"].strip("\0 []")
        if n.startswith("mrm_res_map2"):
            rm = P.parse_resmap(data, sec_start, e, toc)
        elif n.startswith("mrm_hschema"):
            hs = P.parse_hschema(data, sec_start, e)
        elif n.startswith("mrm_decn_info"):
            dn = P.parse_decn(data, sec_start, e)
        elif n.startswith("mrm_dataitem"):
            dis[i] = P.parse_dataitem(data, sec_start, e)
    scopes, items = hs
    decs = dn
    count = 0
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
            fn = P.fullname(scopes, items, it, False)
            if not fn.startswith("\\Files"):
                continue
            dec = decs[decision] if decision < len(decs) else []
            # take the first candidate (language/scale-neutral)
            if not dec:
                continue
            cand = rm["cands"][first_cand] if first_cand < len(rm["cands"]) else None
            if not cand or cand["t"] != 1:
                continue
            if cand["disec"] not in dis:
                continue
            c, cpos, spans, ds, tot = dis[cand["disec"]]
            if cand["di"] >= len(spans):
                continue
            off, ln, kind = spans[cand["di"]]
            raw = c[off:off + ln]
            rel = fn.lstrip("\\").replace("\\", "/")
            dst = os.path.join(outdir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(raw)
            count += 1
            print(f"{rel} ({len(raw)} bytes)")
    print(f"extracted {count} files -> {outdir}")


if __name__ == "__main__":
    main()
