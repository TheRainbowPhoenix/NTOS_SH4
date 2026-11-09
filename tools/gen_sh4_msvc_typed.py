#!/usr/bin/env python3
"""Typed MSVC SH4 CE calling-convention thunks for BE native GCC.

Measured shcl 12.00.8281.0 /Qsh4 C contract. Generate PE export assembly,
native GCC C marshaling, GCC-callable import C wrappers, and an MSVC call gate.
Only fixed-prototype no-FPU C signatures are supported. Never infer types.
"""
import argparse
import json
import re
from pathlib import Path

ARGS = {'u32','ptr','u8','s8','u16','s16','u64','s3','bytes3'}
RETS = ARGS | {'void'}
T = {'u32':'uint32_t','ptr':'void *','u8':'uint8_t','s8':'int8_t',
     'u16':'uint16_t','s16':'int16_t','u64':'uint64_t',
     's3':'NtosAbiS3','bytes3':'NtosAbiBytes3','void':'void'}


def classify(s):
    name, ret, args = s['name'],s['ret'],s['args']
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*',name):
        raise ValueError('invalid symbol '+repr(name))
    if ret not in RETS or not isinstance(args,list) or any(a not in ARGS for a in args):
        raise ValueError('unsupported type/prototype for '+name)
    hidden = ret in ('u64','s3')
    words = int(hidden)+sum(2 if a=='u64' else 3 if a=='s3' else 1 for a in args)
    if words>8:
        raise ValueError('%s has %d words; max is 8'%(name,words))
    return words,hidden


def validate(m):
    if not isinstance(m,list) or not m:
        raise ValueError('nonempty manifest required')
    seen=set()
    for s in m:
        if set(s)!={'name','ret','args'}:
            raise ValueError('expected name/ret/args')
        classify(s)
        if s['name'] in seen:
            raise ValueError('duplicate export '+s['name'])
        seen.add(s['name'])
    return m


def header(m):
    out=['/* Generated: explicit MSVC SH4 C boundary types. */','#pragma once',
         '#include <stdint.h>',
         'typedef struct { uint32_t a,b,c; } NtosAbiS3;',
         'typedef struct { uint8_t a,b,c; } NtosAbiBytes3;',
         '_Static_assert(sizeof(NtosAbiS3)==12,"S3");',
         '_Static_assert(sizeof(NtosAbiBytes3)==3,"bytes3");']
    for s in m:
        args=', '.join(T[a] for a in s['args']) or 'void'
        out+=['extern %s nt_native_%s(%s);'%(T[s['ret']],s['name'],args),
              'extern %s nt_msvc_imp_%s(%s);'%(T[s['ret']],s['name'],args)]
    return '\n'.join(out)+'\n'


def unpack(a,i):
    w='w[%d]'%i
    if a=='u64':return '(((uint64_t)w[%d]<<32)|w[%d])'%(i+1,i),i+2
    if a=='s3':return '(NtosAbiS3){w[%d],w[%d],w[%d]}'%(i,i+1,i+2),i+3
    if a=='bytes3':
        return '(NtosAbiBytes3){(uint8_t)%s,(uint8_t)(%s>>8),(uint8_t)(%s>>16)}'%(w,w,w),i+1
    if a=='ptr':return '(void *)(uintptr_t)%s'%w,i+1
    return '(%s)%s'%(T[a],w),i+1


def pack(a,v):
    if a=='u64':return ['(uint32_t)(%s)'%v,'(uint32_t)((%s)>>32)'%v]
    if a=='s3':return [v+'.a',v+'.b',v+'.c']
    if a=='bytes3':
        return ['((uint32_t)%s.a|((uint32_t)%s.b<<8)|((uint32_t)%s.c<<16))'%(v,v,v)]
    if a=='ptr':return ['(uint32_t)(uintptr_t)%s'%v]
    return ['(uint32_t)%s'%v]


def export_c(m):
    out=['/* Generated native GCC body adapters, no MSVC compiler changes. */',
         '#include "abi_typed.h"']
    for s in m:
        name,ret=s['name'],s['ret']
        _,hidden=classify(s)
        out+=['uint32_t nt_msvc_body_%s(const uint32_t *w,void *result) {'%name,
              '    (void)w;']
        i=int(hidden);params=[]
        for a in s['args']:
            v,i=unpack(a,i);params.append(v)
        call='nt_native_%s(%s)'%(name,', '.join(params))
        if ret=='void':
            out+=['    (void)result;', '    '+call+';', '    return 0;']
        elif hidden:
            out+=['    %s v=%s;'%(T[ret],call),
                  '    if (!result) return 0;',
                  '    *(%s *)result=v;'%T[ret],
                  '    return (uint32_t)(uintptr_t)result;']
        elif ret=='bytes3':
            out+=['    (void)result;', '    NtosAbiBytes3 v=%s;'%call,
                  '    return (uint32_t)v.a|((uint32_t)v.b<<8)|((uint32_t)v.c<<16);']
        elif ret=='ptr':
            out+=['    (void)result;', '    return (uint32_t)(uintptr_t)%s;'%call]
        else:
            out+=['    (void)result;', '    return (uint32_t)%s;'%call]
        out+=['}']
    return '\n'.join(out)+'\n'


def import_c(m):
    out=['/* Generated GCC-native imports and PE IAT slots. */',
         '#include "abi_typed.h"',
         'extern uint32_t nt_msvc_call_typed(const void *,const uint32_t *,uint32_t);']
    for s in m:
        name,ret=s['name'],s['ret']
        words,hidden=classify(s)
        params=', '.join('%s a%d'%(T[a],j) for j,a in enumerate(s['args'])) or 'void'
        out+=['const void *volatile __imp_%s __attribute__((section(".idata")))=(const void *)0;'%name,
              '%s nt_msvc_imp_%s(%s) {'%(T[ret],name,params),
              '    uint32_t w[8]={0};']
        i=0
        if hidden:
            out+=['    %s result={0};'%T[ret] if ret=='s3' else '    uint64_t result=0;',
                  '    w[0]=(uint32_t)(uintptr_t)&result;']
            i=1
        for j,a in enumerate(s['args']):
            for v in pack(a,'a%d'%j):
                out+=['    w[%d]=%s;'%(i,v)]
                i+=1
        out+=['    uint32_t r=nt_msvc_call_typed((const void *)__imp_%s,w,%du);'%(name,words)]
        if ret=='void':out+=['    (void)r;']
        elif hidden:out+=['    (void)r; return result;']
        elif ret=='bytes3':
            out+=['    return (NtosAbiBytes3){(uint8_t)r,(uint8_t)(r>>8),(uint8_t)(r>>16)};']
        elif ret=='ptr':out+=['    return (void *)(uintptr_t)r;']
        else:out+=['    return (%s)r;'%T[ret]]
        out+=['}']
    return '\n'.join(out)+'\n'


def export_asm(m):
    out=['! Generated measured MSVC SH4 exports: R4-R7, S+16 overflow.',
         '    .section .text','    .align 2']
    for s in m:
        name=s['name'];words,hidden=classify(s)
        out+=['    .align 2','    .global _'+name,'_'+name+':',
              '    mov.l r8,@-r15','    stc.l gbr,@-r15','    sts.l pr,@-r15',
              '    mov r4,r8' if hidden else '    mov #0,r8',
              '    add #-32,r15',
              '    mov.l r4,@r15','    mov.l r5,@(4,r15)',
              '    mov.l r6,@(8,r15)','    mov.l r7,@(12,r15)']
        if words>4:
            out+=['    mov r15,r1','    add #60,r1']
            for i in range(4,words):
                out+=['    mov.l @r1+,r0','    mov.l r0,@(%d,r15)'%(i*4)]
        out+=['    mov r15,r4','    mov r8,r5',
              '    mov.l .Lbody_%s,r0'%name,'    jsr @r0','    nop']
        if hidden:out+=['    mov r8,r0']
        out+=['    add #32,r15','    lds.l @r15+,pr','    ldc.l @r15+,gbr',
              '    mov.l @r15+,r8','    rts','    nop','    .align 2',
              '.Lbody_%s: .long _nt_msvc_body_%s'%(name,name)]
    return '\n'.join(out)+'\n'


def call_asm():
    return """! Generated GCC->MSVC SH4 word-stream call gate, max eight words.
! u32 nt_msvc_call_typed(void *fn,const u32 *word_array,u32 n)
    .section .text
    .align 2
    .global _nt_msvc_call_typed
_nt_msvc_call_typed:
    tst r4,r4
    bt .Lbad_call
    mov #8,r0
    cmp/hi r0,r6
    bt .Lbad_call
    mov.l r8,@-r15
    mov.l r9,@-r15
    stc.l gbr,@-r15
    sts.l pr,@-r15
    mov r4,r8
    mov r5,r9
    add #-32,r15
    mov.l @r9+,r4
    mov.l @r9+,r5
    mov.l @r9+,r6
    mov.l @r9+,r7
    mov r15,r1
    add #16,r1
    mov #4,r2
.Lcopy_words:
    mov.l @r9+,r0
    mov.l r0,@r1
    add #4,r1
    dt r2
    bf .Lcopy_words
    mov r8,r0
    jsr @r0
    nop
    add #32,r15
    lds.l @r15+,pr
    ldc.l @r15+,gbr
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
.Lbad_call:
    mov.l .Lbad_value,r0
    rts
    nop
    .align 2
.Lbad_value: .long 0xBADAB100
"""


def render(m,kind):
    m=validate(m)
    return {'header':header,'export-c':export_c,'import-c':import_c,
            'export-asm':export_asm,'call-asm':lambda x:call_asm()}[kind](m)


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--manifest',type=Path,required=True)
    ap.add_argument('--outdir',type=Path,required=True)
    a=ap.parse_args()
    m=validate(json.loads(a.manifest.read_text(encoding='utf-8')))
    a.outdir.mkdir(parents=True,exist_ok=True)
    for kind,name in [('header','abi_typed.h'),('export-c','abi_export_body.c'),
                      ('import-c','abi_import.c'),('export-asm','abi_export.S'),
                      ('call-asm','abi_call.S')]:
        (a.outdir/name).write_text(render(m,kind),encoding='ascii')


if __name__=='__main__':main()
