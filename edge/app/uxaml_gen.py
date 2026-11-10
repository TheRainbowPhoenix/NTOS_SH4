#!/usr/bin/env python3
"""uxaml_gen.py — compile XML markup into .uxaml binary (and back).

Usage:
  uxaml_gen.py build markup/ -o edge.uxaml     # all *.xml -> one container
  uxaml_gen.py toxml edge.uxaml -d SmallScreenPage   # "upper" a doc to XML

The XML accepted is a strict subset mapping 1:1 onto the uXAML binary format:

  <Page name="SmallScreenPage">
    <Canvas>
      <Rect y="480" w="320" h="48" bg="#0078D7" radius="0"/>
      <Text x="12" y="496" text="Search or enter address" color="#FFFFFF" size="12"/>
      <Button x="0" y="480" w="48" h="48" name="TabsButton" command="tabs" text="0"/>
    </Canvas>
  </Page>

Rules:
  - attribute values are plain (X/Y/W/H/Size/Radius/Value/Max/Align = int,
    Bg/Color = #RRGGBB hex, Bold/Checked/Visible = true/false, everything
    else = string)
  - element order == document order; no comments/PIs/namespaces
"""
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

KINDS = {
    'Page': 0, 'Canvas': 1, 'StackV': 2, 'StackH': 3, 'Text': 4, 'Button': 5,
    'Rect': 6, 'Ellipse': 7, 'Glyph': 8, 'List': 9, 'Row': 10, 'TextBox': 11,
    'Progress': 12, 'Check': 13, 'Scroll': 14, 'Toggle': 15,
}
ATTRS = {
    'x': 0, 'y': 1, 'w': 2, 'h': 3, 'text': 4, 'color': 5, 'bg': 6, 'size': 7,
    'bold': 8, 'radius': 9, 'name': 10, 'command': 11, 'src': 12, 'value': 13,
    'max': 14, 'checked': 15, 'visible': 16, 'align': 17, 'border': 18,
    'placeholder': 19,
}
INT_ATTRS = {0, 1, 2, 3, 7, 9, 13, 14, 17}
CLR_ATTRS = {5, 6}
BOOL_ATTRS = {8, 15, 16, 18}
STR_ATTRS = {4, 10, 11, 12, 19}

TYPE_STR, TYPE_INT, TYPE_CLR, TYPE_BOOL = 0, 1, 2, 3


def rgb_to_device(hexc):
    """#RRGGBB -> 0x00BBGGRR (device RGB_ macro order)."""
    h = hexc.lstrip('#')
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return r | (g << 8) | (b << 16)


def device_to_rgb(v):
    r, g, b = v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF
    return f'#{r:02X}{g:02X}{b:02X}'


class Builder:
    def __init__(self):
        self.strings = []
        self.sidx = {}
        self.docs = []  # (name, nodes) ; nodes = list of dicts

    def intern(self, s):
        if s in self.sidx:
            return self.sidx[s]
        if len(self.strings) >= 448:
            raise SystemExit('too many strings')
        self.sidx[s] = len(self.strings)
        self.strings.append(s)
        return len(self.strings) - 1

    def parse_xml(self, path):
        tree = ET.parse(path)
        root = tree.getroot()
        if root.tag != 'Page':
            raise SystemExit(f'{path}: root element must be <Page>')
        name = root.get('name') or os.path.splitext(os.path.basename(path))[0]
        nodes = []

        def walk(elem, depth):
            if elem.tag not in KINDS:
                raise SystemExit(f'{path}: unknown element <{elem.tag}>')
            if len(nodes) >= 384:
                raise SystemExit('too many nodes')
            node = {'kind': KINDS[elem.tag], 'attrs': [], 'children': []}
            for k, v in elem.attrib.items():
                if k not in ATTRS:
                    raise SystemExit(f'{path}: unknown attribute {k}')
                a = ATTRS[k]
                if a in INT_ATTRS:
                    node['attrs'].append((a, TYPE_INT, int(v, 0)))
                elif a in CLR_ATTRS:
                    node['attrs'].append((a, TYPE_CLR, rgb_to_device(v)))
                elif a in BOOL_ATTRS:
                    node['attrs'].append((a, TYPE_BOOL, 1 if v.lower() == 'true' else 0))
                else:
                    node['attrs'].append((a, TYPE_STR, self.intern(v)))
            idx = len(nodes)
            nodes.append(node)
            kids = list(elem)
            if len(kids) > 16:
                raise SystemExit(f'{path}: too many children on <{elem.tag}>')
            node['children'] = [walk(k, depth + 1) for k in kids]
            return idx

        walk(root, 0)
        self.docs.append((name, nodes))

    def emit(self):
        # doc table: name_str, node_offset, node_count  (offsets filled below)
        stream = bytearray()
        doc_entries = []
        for name, nodes in self.docs:
            base = len(stream)
            doc_entries.append((self.intern(name), base, len(nodes)))
            for n in nodes:
                stream.append(n['kind'])
                stream.append(len(n['attrs']))
                for a, t, v in n['attrs']:
                    stream.append(a)
                    stream.append(t)
                    if t == TYPE_STR:
                        stream += struct.pack('<H', v)
                    elif t == TYPE_BOOL:
                        stream.append(v)
                    else:
                        stream += struct.pack('<i' if t == TYPE_INT else '<I', v)
                stream.append(len(n['children']))
        # string heap (len-prefixed; escape long strings with 255 + u16)
        heap = bytearray()
        for s in self.strings:
            b = s.encode('utf-8')
            if len(b) >= 255:
                heap.append(255)
                heap += struct.pack('<H', len(b))
            else:
                heap.append(len(b))
            heap += b
        out = bytearray()
        out += b'UXA1'
        out += struct.pack('<HH', 1, 0)
        out += struct.pack('<HH', len(self.docs), len(self.strings))
        out += struct.pack('<I', len(stream))
        for name_idx, base, count in doc_entries:
            out += struct.pack('<HIH', name_idx, base, count)
        out += heap
        out += stream
        return bytes(out)


def toxml(data):
    """Dump a .uxaml container back to XML files (round-trip proof)."""
    assert data[:4] == b'UXA1', 'not uxaml'
    version, flags = struct.unpack_from('<HH', data, 4)
    docs, strings = struct.unpack_from('<HH', data, 8)
    node_bytes, = struct.unpack_from('<I', data, 12)
    off = 16
    doc_table = []
    for _ in range(docs):
        nidx, noff, ncount = struct.unpack_from('<HIH', data, off)
        doc_table.append((nidx, noff, ncount))
        off += 8
    heap = []
    for _ in range(strings):
        ln = data[off]
        off += 1
        if ln == 255:
            ln, = struct.unpack_from('<H', data, off)
            off += 2
        heap.append(data[off:off + ln].decode('utf-8'))
        off += ln
    stream = data[off:off + node_bytes]
    kind_names = {v: k for k, v in KINDS.items()}
    attr_names = {v: k for k, v in ATTRS.items()}
    out = []
    for nidx, noff, ncount in doc_table:
        p = noff
        nodes = []
        for _ in range(ncount):
            kind = stream[p]
            p += 1
            nattr = stream[p]
            p += 1
            attrs = []
            for _ in range(nattr):
                a, t = stream[p], stream[p + 1]
                p += 2
                if t == TYPE_STR:
                    v = heap[struct.unpack_from('<H', stream, p)[0]]
                    p += 2
                elif t == TYPE_INT:
                    v = struct.unpack_from('<i', stream, p)[0]
                    p += 4
                elif t == TYPE_CLR:
                    v = device_to_rgb(struct.unpack_from('<I', stream, p)[0])
                    p += 4
                else:
                    v = 'true' if stream[p] else 'false'
                    p += 1
                attrs.append((a, v))
            nchild = stream[p]
            p += 1
            nodes.append((kind, attrs, nchild))
        # rebuild tree in document order with a stack; the first node is the
        # Page root — its attrs go on the header line
        root_attrs = ''
        if nodes:
            k0, a0, _ = nodes[0]
            assert k0 == 0, 'doc must be rooted in Page'
            # the doc name lives on the <Page> header; skip its attr here
            root_attrs = ''.join(f' {attr_names[a]}="{v}"' for a, v in a0 if a != 10)
        lines = [f'<Page name="{heap[nidx]}"{root_attrs}>']
        sp = []      # [element kind, expected children, seen children]
        for kind, attrs, nchild in nodes[1:]:
            ind = '  ' * (len(sp) + 1)
            astr = ''.join(f' {attr_names[a]}="{v}"' for a, v in attrs)
            if nchild:
                lines.append(f'{ind}<{kind_names[kind]}{astr}>')
                sp.append([kind, nchild, 0])
            else:
                lines.append(f'{ind}<{kind_names[kind]}{astr}/>')
                if sp:
                    sp[-1][2] += 1
                while sp and sp[-1][2] == sp[-1][1]:
                    k = sp.pop()[0]
                    lines.append('  ' * (len(sp) + 1) + f'</{kind_names[k]}>')
                    if sp:
                        sp[-1][2] += 1
        lines.append('</Page>')
        out.append('\n'.join(lines))
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    mode = sys.argv[1]
    if mode == 'build':
        src = sys.argv[2]
        b = Builder()
        if os.path.isdir(src):
            for fn in sorted(os.listdir(src)):
                if fn.endswith('.xml'):
                    b.parse_xml(os.path.join(src, fn))
        else:
            b.parse_xml(src)
        out = sys.argv[sys.argv.index('-o') + 1]
        data = b.emit()
        open(out, 'wb').write(data)
        print(f'{out}: {len(data)} bytes, {len(b.docs)} docs, '
              f'{len(b.strings)} strings')
        return 0
    if mode == 'toxml':
        path = sys.argv[2]
        data = open(path, 'rb').read()
        xmls = toxml(data)
        if '-d' in sys.argv:
            want = sys.argv[sys.argv.index('-d') + 1]
            for name, x in zip([d for d in re.findall(r'<Page name="([^"]+)"', '\n'.join(xmls))], xmls):
                if name == want:
                    print(x)
                    return 0
            print(f'doc {want} not found', file=sys.stderr)
            return 1
        for x in xmls:
            print(x)
            print()
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
