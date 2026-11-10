#!/usr/bin/env python3
"""Build the full hosted applications as SH4 big-endian device PEs."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import elf2ntx as pack
from generate_apps import icon
from build_phone import add_icon_resource

CC = os.environ.get('PHONE_CC', 'sh4a_nofpueb-elf-gcc')
LD = os.environ.get('PHONE_LD', 'sh4a_nofpueb-elf-g++')
FLAGS = ['-std=c11', '-Os', '-ffreestanding', '-fno-builtin',
         '-fno-stack-protector', '-fno-toplevel-reorder',
         '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
         '-DNTOS_APP_BUILD', '-Iedge/app', '-Ipayload/phone',
         '-Ipayload', '-Ifiles/app', '-Icalc/app']
APPS = {
    'calc': ['calc/app/calc_app.c', 'calc/app/calc_icons.c', 'files/app/files_icons.c', 'edge/app/uxaml.c', 'payload/phone/svf.c'],
    'files': ['files/app/files_app.c', 'files/app/files_icons.c', 'edge/app/uxaml.c', 'payload/phone/uixctl.c', 'payload/phone/svf.c'],
    'cortana': ['cortana/app/cortana_app.c', 'files/app/files_icons.c', 'edge/app/uxaml.c', 'payload/phone/uixctl.c', 'payload/phone/svf.c'],
    'edge': ['edge/app/edge_app.c', 'edge/app/icons.c', 'edge/app/uxaml.c', 'payload/phone/uixctl.c', 'payload/phone/svf.c'],
}
HTML_EXPORTS = ['md_set_host', 'md_load', 'md_layout', 'md_reader', 'md_reader_is',
                'md_paint', 'md_size', 'md_title', 'md_find', 'md_find_text',
                'md_find_style', 'md_link_at', 'md_anchor_scroll']

def run(args):
    print(' '.join(map(str, args)), flush=True)
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='dist')
    ap.add_argument('--obj', default='obj/hosted')
    args = ap.parse_args()
    out, obj = ROOT / args.out, ROOT / args.obj
    obj.mkdir(parents=True, exist_ok=True)
    entry = obj / 'entry.c'
    entry.write_text('extern int RunApplication(void); int main(void) { return RunApplication(); }\n')
    dll_entry = obj / 'dll_entry.c'
    dll_entry.write_text('int main(void) { return 1; }\n')

    def compile_link(name, sources, dll=False):
        objects = []
        for i, source in enumerate(sources):
            target = obj / (name + '-' + str(i) + '.o')
            flags = FLAGS[:]
            if str(source).endswith(('uixctl.c', 'svf.c')):
                flags += ['-DNT_IAT_EXTERN_ONLY', '-DUXCTL_NO_FACTORY']
            run([CC, '-c', source, '-o', target] + flags)
            objects.append(target)
        elf = obj / (name + '.elf')
        run([LD, '-nostdlib', '-Wl,--emit-relocs', '-Wl,-e,_main', '-T',
             ROOT / ('payload/xamlui.ld' if dll else 'payload/payload.ld'),
             '-o', elf, *objects, '-lgcc'])
        return pack.Elf(elf.read_bytes())

    original_imports = pack.build_import_section
    def imports(groups, rva):
        return original_imports([(module, [(slot, name[len('render_'):] if module == 'UIXRENDER.DLL' and name.startswith('render_') else name)
                                           for slot, name in slots]) for module, slots in groups], rva)
    pack.build_import_section = imports
    groups = [('UIXGetSchemaFactory', 'FINGERKB.DLL'),
              ('UIXChromeFactory', 'SHELLCHROME.DLL'),
              ('render_', 'UIXRENDER.DLL'), ('md_', 'HTML.DLL'), ('Js', 'CHAKRA.DLL')]

    for name, source, exports in [
        ('HTML.DLL', 'payload/phone/markdown.c', HTML_EXPORTS),
        ('CHAKRA.DLL', 'edge/app/chakra_stub.c', ['JsRunScript']),
    ]:
        elf = compile_link(name, [source, 'payload/phone/tiny_memory.c', dll_entry], True)
        symbols = {s.name.lstrip('_'): s.value - 0x10000000 for s in elf.symbols if s.shndx != pack.SHN_UNDEF}
        exported = [(n, symbols[n]) for n in exports]
        imported = bool(pack.collect_import_slots(elf))
        image, _ = pack.emit_ntx(elf, 0x10000000, exports=exported, dll_name=name,
                                 import_module='COREDLL.DLL' if imported else None, image_is_dll=True)
        (out / name).write_bytes(image)

    for slug, sources in APPS.items():
        run([sys.executable, ROOT / 'edge/app/uxaml_gen.py', 'build', ROOT / slug / 'app/markup', '-o', out / (slug + '.uxaml')])
        if slug == 'edge':
            run([sys.executable, ROOT / 'edge/app/uxaml_gen.py', 'build', ROOT / 'edge/app/markup', '-o', out / 'edgelight.uxaml', '--light'])
        elf = compile_link(slug, sources + ['payload/phone/tiny_memory.c', entry])
        image, _ = pack.emit_ntx(elf, 0x400000, import_module='COREDLL.DLL', extra_imports=groups)
        dest = out / 'Apps' / slug / (slug + '.exe')
        dest.write_bytes(add_icon_resource(image, icon(slug)))
        print('Built hosted app', dest, flush=True)

    for name in ('article.md', 'logo.svf', 'demo.md', 'cool.svf', 'swap.html'):
        (out / name).write_bytes((ROOT / 'edge/app/tests/assets' / name).read_bytes())

if __name__ == '__main__':
    main()
