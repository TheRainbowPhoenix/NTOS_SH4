#!/usr/bin/env python3
"""xbf_dump.py — XBF v2 (XAML Binary Format) decoder, ported from
chausner/XbfTools XbfFormat/XbfReader.cs.

Decodes the compiled XAML pages embedded in a W10M resources.pri into a
readable indented object tree (types, x:Name, properties incl. colors,
thicknesses, enums, StaticResource/ThemeResource references, styles,
data templates, visual states).

Usage:
  python3 xbf_dump.py file.xbf              # dump tree to stdout
  python3 xbf_dump.py file.xbf -o out.txt
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from xbf_framework_types import type_names, property_names, enum_values


class R:
    def __init__(self, d, p=0):
        self.d, self.p = d, p

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]; self.p += 4; return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v

    def u64(self):
        v = struct.unpack_from("<Q", self.d, self.p)[0]; self.p += 8; return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.p)[0]; self.p += 4; return v

    def r7(self):
        """7-bit encoded int (BinaryReader.Read7BitEncodedInt)"""
        result = 0
        shift = 0
        while True:
            b = self.d[self.p]; self.p += 1
            result |= (b & 0x7F) << shift
            if not (b & 0x80):
                return result
            shift += 7
            if shift >= 35:
                raise ValueError("bad 7-bit int")

    def xbfstr(self):
        n = self.i32()
        s = self.d[self.p:self.p + n * 2].decode("utf-16-le")
        self.p += n * 2
        return s


class Node:
    def __init__(self, type_name=None):
        self.type = type_name
        self.name = None
        self.uid = None
        self.key = None
        self.conn = None
        self.props = []       # (name, value) — value may be a Node or list
        self.children = []

    def add_prop(self, n, v):
        self.props.append((n, v))


def fmt_float(v):
    if isinstance(v, float):
        return f"{v:g}"
    return str(v)


class Xbf:
    def __init__(self, data):
        self.d = data
        r = R(data)
        magic = data[:4]
        assert magic == b"XBF\x00", "not XBF"
        r.p = 4  # skip magic
        self.metadata_size = r.u32()
        self.node_size = r.u32()
        self.major = r.u32()
        self.minor = r.u32()
        self.str_off = r.u64()
        self.asm_off = r.u64()
        self.tns_off = r.u64()
        self.type_off = r.u64()
        self.prop_off = r.u64()
        self.xmlns_off = r.u64()
        self.hash = data[r.p:r.p + 64].decode("latin1")
        assert self.major >= 2, "only XBF v2"
        # NB: table offsets in the header are relative to file offset 0xC
        BASE = 0xC
        # tables are sequential from their offsets; use table counts
        r = R(data, BASE + self.str_off)
        self.strings = self._read_string_table(r)
        self.str_end = r.p
        # assemblies
        r = R(data, BASE + self.asm_off)
        self.assemblies = []
        for _ in range(r.u32()):
            kind = r.i32(); sid = r.i32()
            self.assemblies.append((kind, self.strings[sid]))
        # type namespaces
        r = R(data, BASE + self.tns_off)
        self.tns = []
        for _ in range(r.u32()):
            aid = r.i32(); sid = r.i32()
            self.tns.append(self.strings[sid])
        # types
        r = R(data, BASE + self.type_off)
        self.types = []
        for _ in range(r.u32()):
            flags = r.i32(); nsid = r.i32(); sid = r.i32()
            if flags & 2:
                self.types.append((flags, None, self.strings[sid]))
            else:
                self.types.append((flags, self.tns[nsid], self.strings[sid]))
        # properties
        r = R(data, BASE + self.prop_off)
        self.props = []
        for _ in range(r.u32()):
            r.i32(); tid = r.i32(); sid = r.i32()
            self.props.append(self.strings[sid])
        # xml namespaces
        r = R(data, BASE + self.xmlns_off)
        self.xmlns = []
        for _ in range(r.u32()):
            self.xmlns.append(self.strings[r.i32()])
        self.xmlns_end = r.p
        # node sections
        r = R(data, self.xmlns_end)
        self.node_sections = []
        for _ in range(r.i32()):
            self.node_sections.append((r.i32(), r.i32()))
        self.first_node_pos = r.p

    def _read_string_table(self, r):
        n = r.u32()
        out = []
        for _ in range(n):
            out.append(r.xbfstr())
            assert r.u16() == 0  # v2 extra nulls
        return out

    # ---- name resolution ----
    def type_name(self, tid):
        if tid & 0x8000:
            i = (tid & ~0x8000) - 1
            if 0 <= i < len(type_names) and type_names[i]:
                return type_names[i]
            return f"UnknownType0x{tid:X4}"
        fl, ns, nm = self.types[tid]
        if ns:
            return f"using:{ns}:{nm}"
        return nm

    def prop_name(self, pid):
        if pid & 0x8000:
            i = (pid & ~0x8000) - 1
            if 0 <= i < len(property_names) and property_names[i]:
                return property_names[i]
            return f"UnknownProperty0x{pid:X4}"
        return self.props[pid]

    def enum_name(self, eid, val):
        ev = enum_values.get(eid)
        if ev:
            if val in ev:
                return ev[val]
            names = [ev[v] for v in sorted(ev) if v != 0 and (val & v) == v]
            known = 0
            for v in ev:
                known |= v
            if names and (val & ~known) == 0:
                return ",".join(names)
        return f"(Enum0x{eid:X4}){val}"

    # ---- property values ----
    def value(self, r):
        t = r.u8()
        if t == 0x01:
            return False
        if t == 0x02:
            return True
        if t == 0x03:
            return r.f32()
        if t == 0x04:
            return r.i32()
        if t == 0x05:
            return self.strings[r.u16()]
        if t == 0x06:
            l, tp, rt, b = r.f32(), r.f32(), r.f32(), r.f32()
            if l == rt and tp == b:
                return fmt_float(l) if l == tp else f"{fmt_float(l)},{fmt_float(tp)}"
            return f"{fmt_float(l)},{fmt_float(tp)},{fmt_float(rt)},{fmt_float(b)}"
        if t == 0x07:
            gl_type = r.i32()
            gl_val = r.f32()
            if gl_type == 0:
                return "Auto"
            if gl_type == 1:
                return fmt_float(gl_val)
            if gl_type == 2:
                return "*" if gl_val == 1 else f"{fmt_float(gl_val)}*"
            raise ValueError("gridlength")
        if t == 0x08:
            b, g, rr, a = r.u8(), r.u8(), r.u8(), r.u8()
            return f"#{a:02X}{rr:02X}{g:02X}{b:02X}"
        if t == 0x09:
            return r.xbfstr()
        if t == 0x0A:
            return ""
        if t == 0x0B:
            eid = r.u16()
            val = r.i32()
            return self.enum_name(eid, val)
        raise ValueError(f"unrecognized value type 0x{t:02X} @{r.p-1:#x}")

    # ---- node stream ----
    def read_root(self, r, end):
        root = Node()
        self.root_stack = [root]
        self.obj_stack = [root]
        self.col_stack = [root.children]
        self.prefixes = {}
        while r.p < end:
            cmd = r.u8()
            if cmd in (0x12, 0x03):
                ns = self.xmlns[r.u16()]
                prefix = r.xbfstr()
                self.prefixes[ns] = prefix
                root.add_prop(("xmlns:" + prefix) if prefix else "xmlns", ns)
            elif cmd == 0x0B:
                root.add_prop("x:Class", r.xbfstr())
            elif cmd == 0x17:
                root.type = self.type_name(r.u16())
                self.read_nodes(r, end)
                break
            else:
                raise ValueError(f"root stream cmd 0x{cmd:02X} @{r.p-1:#x}")
        assert self.obj_stack[-1] is root
        return self.root_stack.pop()

    def read_nodes(self, r, end, single_object=False, single_node=False):
        single = None
        while r.p < end:
            cmd = r.u8()
            if cmd == 0x01:
                pass
            elif cmd == 0x04:
                if self.col_stack[-1] is not self.obj_stack[-1].children:
                    text = self.value(r)
                    obj = Node("Verbatim")
                    obj.add_prop("Value", text)
                    self.obj_stack.append(obj)
                elif self.obj_stack[-1] is self.root_stack[-1]:
                    self.obj_stack[-1].add_prop("x:Class", self.value(r))
                else:
                    self.value(r)
            elif cmd == 0x0C:
                self.obj_stack[-1].conn = self.value(r)
            elif cmd == 0x0D:
                self.obj_stack[-1].name = str(self.value(r))
            elif cmd == 0x0E:
                self.obj_stack[-1].uid = str(self.value(r))
            elif cmd == 0x11:
                self.read_data_template(r)
            elif cmd in (0x1A, 0x1B):
                pn = self.prop_name(r.u16())
                pv = self.value(r)
                self.obj_stack[-1].add_prop(pn, pv)
            elif cmd == 0x1C:
                pn = self.prop_name(r.u16())
                rpn = self.prop_name(r.u16())
                self.obj_stack[-1].add_prop(pn, rpn)
            elif cmd == 0x1D:
                pn = self.prop_name(r.u16())
                tn = self.type_name(r.u16())
                self.obj_stack[-1].add_prop(pn, tn)
            elif cmd == 0x1E:
                pn = self.prop_name(r.u16())
                pv = self.value(r)
                self.obj_stack[-1].add_prop(pn, "{StaticResource %s}" % pv)
            elif cmd == 0x1F:
                pn = self.prop_name(r.u16())
                bp = self.prop_name(r.u16())
                self.obj_stack[-1].add_prop(pn, "{TemplateBinding %s}" % bp)
            elif cmd == 0x24:
                pn = self.prop_name(r.u16())
                pv = self.value(r)
                self.obj_stack[-1].add_prop(pn, "{ThemeResource %s}" % pv)
            elif cmd == 0x22:
                pv = self.value(r)
                obj = Node("StaticResource")
                obj.add_prop("ResourceKey", pv)
                self.obj_stack.append(obj)
                if single_object:
                    return
            elif cmd == 0x23:
                pv = self.value(r)
                obj = Node("ThemeResource")
                obj.add_prop("ResourceKey", pv)
                self.obj_stack.append(obj)
                if single_object:
                    return
            elif cmd == 0x13:
                pn = self.prop_name(r.u16())
                coll = []
                self.obj_stack[-1].add_prop(pn, coll)
                self.col_stack.append(coll)
            elif cmd == 0x02:
                self.col_stack.pop()
            elif cmd == 0x14:
                obj = Node(self.type_name(r.u16()))
                self.obj_stack.append(obj)
                self.col_stack.append(obj.children)
                if single_object and single is None:
                    single = obj
            elif cmd in (0x21, 0x28):
                if self.col_stack and self.col_stack[-1] is self.obj_stack[-1].children:
                    self.col_stack.pop()
                if cmd == 0x28:
                    return
                if single_object and self.obj_stack[-1] is single:
                    return
                if self.obj_stack[-1] is self.root_stack[-1]:
                    return
            elif cmd in (0x07, 0x20):
                pn = self.prop_name(r.u16())
                obj = self.obj_stack.pop()
                self.obj_stack[-1].add_prop(pn, obj)
            elif cmd in (0x08, 0x09):
                obj = self.obj_stack.pop()
                self.col_stack[-1].append(obj)
            elif cmd == 0x0A:
                obj = self.obj_stack.pop()
                obj.key = str(self.value(r))
                self.col_stack[-1].append(obj)
            elif cmd == 0x0B:
                self.obj_stack[-1].add_prop("x:Class", r.xbfstr())
            elif cmd in (0x15, 0x16):
                obj = Node(self.type_name(r.u16()))
                obj.add_prop("Value", self.value(r))
                self.obj_stack.append(obj)
                if single_object and single is None:
                    single = obj
            elif cmd == 0x0F:
                self.read_section_ref(r)
            elif cmd in (0x12, 0x17):
                r.p -= 1
                self.read_root(r, end)
                if single_object and single is None:
                    return
            elif cmd in (0x18, 0x19):
                tn = self.type_name(r.u16())
                arg = self.value(r)
                self.obj_stack[-1].add_prop("x:Class", tn)
                self.obj_stack[-1].add_prop("x:Arguments", arg)
            elif cmd == 0x26:
                self.type_name(r.u16())
                self.strings[r.u16()]
            elif cmd == 0x27:
                pass
            elif cmd == 0x8B:
                self.obj_stack.pop()
            else:
                raise ValueError(f"node cmd 0x{cmd:02X} @{r.p-1:#x}")
            if single_node:
                break

    # ---- secondary sections ----
    def read_section_ref(self, r):
        sec = self.node_sections[r.r7()]
        assert r.u16() == 0
        t = r.r7()
        if t in (2, 8):
            self.read_style(r, sec, False)
        elif t == 11:
            self.read_style(r, sec, True)
        elif t == 7:
            self.read_resdict(r, sec, False, False)
        elif t == 371:
            self.read_resdict(r, sec, True, False)
        elif t == 10:
            self.read_resdict(r, sec, True, True)
        elif t == 5:
            self.skip_visual_states(r)
            self.read_node_section(sec)
        elif t == 6:
            self.read_deferred(r, sec, True, False)
        elif t == 746:
            self.read_deferred(r, sec, False, False)
        elif t == 9:
            self.read_deferred(r, sec, True, True)
        else:
            raise ValueError(f"unknown section ref type {t}")

    def read_data_template(self, r):
        pn = self.prop_name(r.u16())
        sec = self.node_sections[r.r7()]
        src_count = r.r7()
        theme_count = r.r7()
        for _ in range(src_count):  # static resource refs
            self.strings[r.u16()]
        for _ in range(theme_count):  # theme resource refs
            self.strings[r.u16()]
        self.read_node_section(sec)
        obj = self.obj_stack.pop()
        self.obj_stack[-1].add_prop(pn, obj)

    def read_style(self, r, sec, extended):
        for _ in range(r.r7()):
            vt = r.u8()
            pn = None
            offset = 0
            pv = None
            if vt in (0x01, 0x02, 0x08):
                pn = self.strings[r.u16()]
                self.type_name(r.u16())
                offset = r.r7()
            elif vt in (0x11, 0x12, 0x18):
                pn = self.prop_name(r.u16())
                offset = r.r7()
            elif vt == 0x20:
                pn = self.strings[r.u16()]
                self.type_name(r.u16())
                pv = self.value(r)
            elif vt == 0x30:
                pn = self.prop_name(r.u16())
                pv = self.value(r)
            elif vt in (0x40, 0x50):
                if not extended:
                    pn = self.prop_name(r.u16())
                offset = r.r7()
            elif vt in (0xC0, 0xD0):
                assert r.r7() == 1
                if not extended:
                    pn = self.prop_name(r.u16())
                offset = r.r7()
            else:
                raise ValueError(f"setter type {vt:#x}")

            if vt in (0x01, 0x02, 0x11, 0x12):
                setter = Node("Setter")
                setter.add_prop("Property", pn)
                self.col_stack[-1].append(setter)
                self.obj_stack.append(setter)
                self.read_node_in_section(r, sec, offset)
                self.obj_stack.pop()
            elif vt in (0x08, 0x18):
                pv = self.read_object_in_section(r, sec, offset)
                setter = Node("Setter")
                setter.add_prop("Property", pn)
                setter.add_prop("Value", pv)
                self.col_stack[-1].append(setter)
            elif vt in (0x20, 0x30):
                setter = Node("Setter")
                setter.add_prop("Property", pn)
                setter.add_prop("Value", pv)
                self.col_stack[-1].append(setter)
            elif vt in (0x40, 0x50, 0xC0, 0xD0):
                obj = self.read_object_in_section(r, sec, offset)
                self.col_stack[-1].append(obj)
        if extended:
            assert r.r7() == 0

    def read_deferred(self, r, sec, extended, extended2):
        self.strings[r.u16()]
        if extended:
            for _ in range(r.r7()):
                self.prop_name(r.u16())
                self.value(r)
        self.read_node_section(sec)
        child = self.obj_stack.pop()
        self.obj_stack[-1].children.append(child)
        if extended2:
            r.r7()

    def read_resdict(self, r, sec, extended, extended2):
        for _ in range(r.r7()):  # keyed resources
            key = self.strings[r.u16()]
            pos = r.r7()
            obj = self.read_object_in_section(r, sec, pos)
            obj.key = key
            self.col_stack[-1].append(obj)
        for _ in range(r.r7()):  # repeated keys
            self.strings[r.u16()]
        for _ in range(r.r7()):  # keyed-by-targettype styles
            self.strings[r.u16()]
            pos = r.r7()
            obj = self.read_object_in_section(r, sec, pos)
            self.col_stack[-1].append(obj)
        if not extended2:
            if extended:
                assert r.r7() == 0
            for _ in range(r.r7()):
                self.strings[r.u16()]
        else:
            assert r.r7() == 0
            assert r.r7() == 0
            assert r.r7() == 0

    def skip_visual_states(self, r):
        n = r.r7()
        memberships = [r.r7() for _ in range(n)]
        n2 = r.r7()
        assert n == n2
        for _ in range(n2):
            r.u16()      # name id
            r.r7(); r.r7()
            for _ in range(r.r7()):
                r.r7()   # setter offsets
            for _ in range(r.r7()):   # adaptive triggers
                for _ in range(r.r7()):
                    r.r7()
            for _ in range(r.r7()):   # state triggers
                r.r7()
            for _ in range(r.r7()):   # offsets (0 or 2)
                r.r7()
            assert r.r7() == 0
        for _ in range(r.r7()):   # groups
            r.u16()
            r.r7()
            r.r7()          # group object offset
        for _ in range(r.r7()):   # transitions
            r.u16(); r.u16(); r.r7()
        r.r7()
        for _ in range(r.r7()):
            r.r7(); r.r7(); r.r7()
        for _ in range(r.r7()):
            r.r7()
        r.r7()
        for _ in range(r.r7()):   # string refs
            r.u16()

    def read_node_section(self, sec):
        orig = R(self.d, 0)
        p = r_save(self)
        r = R(self.d, self.first_node_pos + sec[0])
        end = self.first_node_pos + sec[1]
        self.read_nodes(r, end)
        r_restore(self, p)

    def read_object_in_section(self, r, sec, offset):
        p = r.p
        nr = R(self.d, self.first_node_pos + sec[0] + offset)
        depth_obj = len(self.obj_stack)
        depth_col = len(self.col_stack)
        self.read_nodes(nr, 1 << 30, single_object=True)
        obj = self.obj_stack.pop()
        assert len(self.obj_stack) == depth_obj
        assert len(self.col_stack) == depth_col
        r.p = p
        return obj

    def read_node_in_section(self, r, sec, offset):
        p = r.p
        nr = R(self.d, self.first_node_pos + sec[0] + offset)
        depth_obj = len(self.obj_stack)
        depth_col = len(self.col_stack)
        self.read_nodes(nr, 1 << 30, single_object=False, single_node=True)
        assert len(self.obj_stack) == depth_obj
        assert len(self.col_stack) == depth_col
        r.p = p

    def read_root_section(self):
        sec = self.node_sections[0]
        r = R(self.d, self.first_node_pos + sec[0])
        end = self.first_node_pos + sec[1]
        return self.read_root(r, end)


def r_save(_):
    return None


def r_restore(_r, _p):
    pass


def dump(obj, ind=0, out=None):
    line = "  " * ind
    attrs = []
    if obj.name:
        attrs.append(f"x:Name={obj.name}")
    if obj.uid:
        attrs.append(f"x:Uid={obj.uid}")
    if obj.key:
        attrs.append(f"Key={obj.key}")
    if obj.conn is not None:
        attrs.append(f"#conn{obj.conn}")
    out.append(f"{line}{obj.type or '?'}" + (" [" + ", ".join(attrs) + "]" if attrs else ""))
    for pn, pv in obj.props:
        if isinstance(pv, Node):
            out.append(f"{line}  .{pn}:")
            dump(pv, ind + 2, out)
        elif isinstance(pv, list):
            out.append(f"{line}  .{pn}: [")
            for item in pv:
                if isinstance(item, Node):
                    dump(item, ind + 2, out)
                else:
                    out.append("  " * (ind + 2) + repr(item))
            out.append(f"{line}  ]")
        else:
            out.append(f"{line}  .{pn} = {pv!r}")
    for ch in obj.children:
        dump(ch, ind + 1, out)


def main():
    path = sys.argv[1]
    outp = None
    if "-o" in sys.argv:
        outp = sys.argv[sys.argv.index("-o") + 1]
    data = open(path, "rb").read()
    x = Xbf(data)
    root = x.read_root_section()
    lines = []
    dump(root, 0, lines)
    text = "\n".join(lines) + "\n"
    if outp:
        open(outp, "w").write(text)
        print(f"{path}: {len(lines)} lines -> {outp}")
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
