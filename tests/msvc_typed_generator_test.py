#!/usr/bin/env python3
"""Fail-closed typed MSVC SH4 generator and PE directory tests.

No assertion here substitutes for running ABITYPED.EXE in the CP400 emulator.
"""
import importlib.util
import json
import pathlib
import sys

ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sh4_ce_typed',ROOT/'tools/gen_sh4_msvc_typed.py')
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
manifest=json.loads((ROOT/'payload/abi_typed_manifest.json').read_text())

assert len(mod.validate(manifest))>=18
for item in manifest:
    n,hidden=mod.classify(item)
    assert 0 <= n <= 8
    assert hidden == (item['ret'] in ('u64','s3'))
for bad in [
    [{'name':'broken','ret':'float','args':[]}],
    [{'name':'broken','ret':'double','args':[]}],
    [{'name':'broken','ret':'u32','args':['varargs']}],
    [{'name':'broken','ret':'u32','args':['callback']}],
    [{'name':'broken','ret':'u32','args':['f64struct']}],
    [{'name':'broken','ret':'u32','args':['u32']*9}],
    [{'name':'duplicated','ret':'u32','args':[]},
     {'name':'duplicated','ret':'u32','args':[]}],
]:
    try:
        mod.validate(bad)
    except ValueError:
        pass
    else:
        raise AssertionError('unsupported ABI signature was accepted: '+repr(bad))

exp=mod.render(manifest,'export-asm')
imp=mod.render(manifest,'import-c')
call=mod.render(manifest,'call-asm')
body=mod.render(manifest,'export-c')
for item in manifest:
    name=item['name']
    assert '_'+name+':' in exp
    assert '__imp_'+name in imp
    assert 'nt_msvc_imp_'+name in imp
    assert 'nt_msvc_body_'+name in body
assert '    add #60,r1' in exp  # entry S+16 after 44 bytes of private frame
assert '    add #16,r1' in call # outgoing home at S+0..15
assert '    cmp/hi r0,r6' in call # reject count > 8
assert 'uint32_t w[8]={0};' in imp # gate reads eight initialized words
assert '((uint64_t)w[' in body # reconstruct high/low pair
assert 'uint32_t)((' in imp # check high word emitted
assert 'NtosAbiS3 v=' in body # hidden aggregate result through native GCC
assert 'NtosAbiBytes3 v=' in body # packed 3-byte scalar return
assert 'NtosAbiBytes1 v=' in body # packed 1-byte scalar return
assert 'NtosAbiBytes4 v=' in body # packed 4-byte scalar return

if '--pe' in sys.argv:
    import pefile
    dll=pefile.PE(str(ROOT/'dist_msvc_typed/ABITYPED.DLL'))
    exe=pefile.PE(str(ROOT/'dist_msvc_typed/ABITYPED.EXE'))
    assert dll.FILE_HEADER.Machine==0x01A8
    assert exe.FILE_HEADER.Machine==0x01A8
    assert dll.OPTIONAL_HEADER.Subsystem==9
    expected={x['name'] for x in manifest}
    exported={s.name.decode('ascii') for s in dll.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
    assert expected <= exported,(expected-exported)
    imports={e.dll.decode('ascii').upper():e for e in exe.DIRECTORY_ENTRY_IMPORT}
    assert 'ABITYPED.DLL' in imports
    imported={s.name.decode('ascii') for s in imports['ABITYPED.DLL'].imports if s.name}
    assert expected <= imported,(expected-imported)
print('PASS typed MSVC SH4 generator: 18 signatures, strict rejection, PE export/IAT')
