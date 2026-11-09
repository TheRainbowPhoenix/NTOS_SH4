"""Compact scalar MSVC export boundaries, using upstream's measured call contract.

Public signatures are explicit JSON. Internal Ce* broker exports stay GCC-native.
Unknown signatures never acquire a guessed word count. No FP/struct-by-value ABI.
"""
import importlib.util
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('upstream_msvc',ROOT/'tools/gen_sh4_msvc_typed.py')
upstream=importlib.util.module_from_spec(spec);spec.loader.exec_module(upstream)

def generate(manifest,exports,defined,stem):
    signatures={entry['ordinal']:entry for entry in manifest}
    out=['.text','.align 2','_ce_dispatch:',
         'mov.l r8,@-r15','stc.l gbr,@-r15','sts.l pr,@-r15',
         'add #-64,r15','mov r0,r8','mov r1,r2','add #-4,r2',
         'cmp/pl r2','bf .Lfixed_call','mov r15,r3','add #92,r3','mov r15,r1',
         '.Lcopy_extra:','mov.l @r3+,r0','mov.l r0,@r1','add #4,r1','dt r2','bf .Lcopy_extra',
         '.Lfixed_call:','jsr @r8','nop','add #64,r15','lds.l @r15+,pr',
         'ldc.l @r15+,gbr','mov.l @r15+,r8','rts','nop',
         '.align 2','_ce_variadic:',
         'mov.l r8,@-r15','stc.l gbr,@-r15','sts.l pr,@-r15',
         'mov r0,r8','mov r15,r1','add #12,r1',
         'mov.l r4,@r1','mov.l r5,@(4,r1)','mov.l r6,@(8,r1)','mov.l r7,@(12,r1)',
         'mov r1,r4','jsr @r8','nop','lds.l @r15+,pr','ldc.l @r15+,gbr',
         'mov.l @r15+,r8','rts','nop']
    implemented=[]
    for index,e in enumerate(exports):
        ordinal=e['ordinal'];entry=signatures.get(ordinal);ok=entry is not None
        implemented.append((ordinal,e['name'],ok))
        out+=['.align 2',f'.global _ce_export_{ordinal}',f'_ce_export_{ordinal}:']
        if not ok:
            bank=index//128
            out+=['mov.w 1f,r4',f'bra .Lunsupported_bank_{bank}','nop',f'1: .word {ordinal}']
        else:
            pass
        # Place one dispatcher island per 128 exports. Every branch stays in range.
        if not ok:
            if index%128==127 or index==len(exports)-1:
                target='_CeUnsupported' if stem=='core' else '_cc_unimplemented'
                out+=['.align 2',f'.Lunsupported_bank_{index//128}:','mov.l 1f,r0','mov #1,r1','mov.l 2f,r2','jmp @r2','nop','.align 2',f'1: .long {target}','2: .long _ce_dispatch']
            continue
        n=len(entry['args']);ret=entry['ret'];abi=entry['abi']
        if ret not in ('u32','ptr','void') or any(t not in ('u32','ptr') for t in entry['args']):
            raise ValueError('non-word signature requires upstream typed marshaling: '+str(entry))
        if n<=8:
            upstream.validate([{'name':f'ce{ordinal}','ret':ret,'args':entry['args']}])
        elif n>16:raise ValueError('more than sixteen words')
        target=entry['target'];dispatch='_ce_dispatch'
        if abi=='varargs-words':
            if target not in ('wsprintfW','StringCbPrintfW'):raise ValueError('unclassified variadic')
            target='CeMsvc_'+target+'Words';dispatch='_ce_variadic'
        elif abi not in ('fixed-words','va-list-words'):raise ValueError('unclassified ABI')
        if '_'+target not in defined:raise ValueError('missing native implementation: '+target)
        out+=['mov.l 1f,r0',f'mov #{n},r1','mov.l 2f,r2','jmp @r2','nop','.align 2',f'1: .long _{target}',f'2: .long {dispatch}']
        if index%128==127 or index==len(exports)-1:
            target='_CeUnsupported' if stem=='core' else '_cc_unimplemented'
            out+=['.align 2',f'.Lunsupported_bank_{index//128}:','mov.l 1f,r0','mov #1,r1','mov.l 2f,r2','jmp @r2','nop','.align 2',f'1: .long {target}','2: .long _ce_dispatch']
    return '\n'.join(out)+'\n',implemented
