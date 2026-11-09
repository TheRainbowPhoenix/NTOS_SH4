#!/usr/bin/env python3
"""SH4LE Windows CE PE analyzer and *manual-range* BE instruction converter.

Research tool, not general binary recompilation. PE metadata stays LE. Never
swap an entire .text section: it contains inline literal words and code.
Original LE binaries cannot simply execute on a BE SH4AL-DSP, especially FPU.
"""
import argparse
import json
import struct
from pathlib import Path

SH4LE=0x01A6
NTOS_SH4BE_LEGACY=0x01A8  # NTOS private convention; Microsoft uses SH5


def parse(data):
    if len(data)<0x100 or data[:2]!=b'MZ':
        raise ValueError("not MZ/PE")
    pe=struct.unpack_from('<I',data,0x3c)[0]
    if pe+24>len(data) or data[pe:pe+4]!=b'PE\0\0':
        raise ValueError("invalid PE signature")
    machine,n=struct.unpack_from('<HH',data,pe+4)
    size=struct.unpack_from('<H',data,pe+20)[0]
    opt=pe+24
    if opt+size+40*n>len(data) or size<96 or struct.unpack_from('<H',data,opt)[0]!=0x10b:
        raise ValueError("unsupported or truncated PE32")
    sections=[]
    for i in range(n):
        off=opt+size+i*40
        name=data[off:off+8].split(b'\0')[0].decode('ascii','replace')
        vsize,rva,raw,fo=struct.unpack_from('<IIII',data,off+8)
        flags=struct.unpack_from('<I',data,off+36)[0]
        if raw and (fo>len(data) or raw>len(data)-fo):
            raise ValueError("section outside file: "+name)
        sections.append(dict(name=name,virtual_size=vsize,rva=rva,
                             raw_size=raw,offset=fo,flags=flags))
    count=struct.unpack_from('<I',data,opt+92)[0]
    dirs=[]
    for i in range(min(16,count,(size-96)//8)):
        rva,sz=struct.unpack_from('<II',data,opt+96+i*8)
        if rva or sz: dirs.append(dict(index=i,rva=rva,size=sz))
    return dict(machine=machine,pe_offset=pe,
                image_base=struct.unpack_from('<I',data,opt+28)[0],
                entry_rva=struct.unpack_from('<I',data,opt+16)[0],
                subsystem=struct.unpack_from('<H',data,opt+68)[0],
                sections=sections,directories=dirs)


def rva_offset(p,rva):
    for s in p['sections']:
        if s['rva']<=rva<s['rva']+s['raw_size']:
            return s['offset']+rva-s['rva']
    return None


def read_imports(data,p):
    dirs={x['index']:x for x in p['directories']}
    if 1 not in dirs:return []
    start=rva_offset(p,dirs[1]['rva'])
    if start is None:return [{'error':'import descriptor outside section'}]
    out=[]
    for i in range(min(256,dirs[1]['size']//20)):
        off=start+i*20
        if off+20>len(data):break
        oft,_,_,name,iat=struct.unpack_from('<IIIII',data,off)
        if not any((oft,name,iat)):break
        no=rva_offset(p,name)
        if no is None:out.append({'error':'bad DLL name RVA'});continue
        dll=data[no:no+256].split(b'\0')[0].decode('ascii','replace')
        to=rva_offset(p,oft or iat)
        items=[]
        if to is not None:
            for j in range(2048):
                if to+j*4+4>len(data):break
                v=struct.unpack_from('<I',data,to+j*4)[0]
                if not v:break
                if v&0x80000000:
                    items.append({'ordinal':v&0xffff})
                else:
                    hn=rva_offset(p,v)
                    if hn is None:items.append({'error':'bad import hint/name RVA'})
                    else:items.append({'name':data[hn+2:hn+258].split(b'\0')[0].decode('ascii','replace')})
        out.append({'dll':dll,'total':len(items),
                    'ordinal_count':sum('ordinal' in x for x in items),
                    'name_count':sum('name' in x for x in items),
                    'imports':items})
    return out


def inspect(data,reference=None):
    p=parse(data)
    code=[s for s in p['sections'] if s['flags']&0x20000000]
    found=[];nwords=0
    for s in code:
        buf=data[s['offset']:s['offset']+min(s['raw_size'],s['virtual_size'])]
        for i in range(0,len(buf)-1,2):
            word=int.from_bytes(buf[i:i+2],'little')
            nwords+=1
            # 0xF*** family includes SH4 FPU operations, but a linear scan
            # also sees *data*; report candidates, not proven executions.
            if (word&0xf000)==0xf000:
                found.append(dict(section=s['name'],rva=hex(s['rva']+i),word=hex(word)))
    d={x['index'] for x in p['directories']}
    risks=[
      "Executable sections may contain inline 32-bit literal pools; instruction-only ranges required",
      "PE structures/resources remain little endian; raw pointers and SH4 data must be converted selectively",
      "A swapped FPU opcode cannot execute on SH4AL-DSP without software translation",
    ]
    if 1 in d:risks.append("PE imports/IAT need NTOS-compatible snapping and MSVC ABI adapters")
    if 3 in d:risks.append(".pdata exception/unwind tables require separate validation")
    if 5 not in d:risks.append("no base relocation directory; image might require its preferred base")
    imported=read_imports(data,p)
    if any(x.get('ordinal_count',0) for x in imported):
        risks.append("ordinal-only DLL imports require PE ordinal resolution and correct export ordinal tables")
    result=dict(dependencies=imported,input=p,executable_halfwords_scanned=nwords,
                fpu_family_candidates_count=len(found),
                fpu_family_candidates_first_32=found[:32],
                warnings=risks,safe_for_automatic_conversion=False)
    if reference:
        q=parse(reference)
        stats=[]
        for s in code:
            t=next((x for x in q['sections'] if x['name']==s['name'] and x['rva']==s['rva']),None)
            if not t:continue
            count=min(s['raw_size'],t['raw_size'])
            a=data[s['offset']:s['offset']+count]
            b=reference[t['offset']:t['offset']+count]
            equal=sum(x==y for x,y in zip(a,b))
            swapped=sum(a[i+j]==b[i+1-j] for i in range(0,count-1,2) for j in (0,1))
            stats.append(dict(section=s['name'],bytes_compared=count,
                              identical_bytes=equal,halfword_swapped_bytes=swapped))
        result['reference_comparison']=dict(machine=hex(q['machine']),
                                           entry_rva=hex(q['entry_rva']),sections=stats)
    return result


def convert_manual(data,ranges):
    p=parse(data)
    if p['machine']!=SH4LE:raise ValueError("input must be Microsoft SH4LE 0x01A6")
    if not isinstance(ranges,list) or not ranges:
        raise ValueError("explicit reviewed code ranges required")
    spans=[]
    for pair in ranges:
        if not isinstance(pair,list) or len(pair)!=2 or any(not isinstance(v,int) for v in pair):
            raise ValueError("range must be two integer RVAs")
        a,b=pair
        if a<0 or a>=b or (a|b)&1:raise ValueError("unaligned/empty instruction range")
        sec=[s for s in p['sections'] if s['flags']&0x20000000
             and s['rva']<=a<b<=s['rva']+min(s['raw_size'],s['virtual_size'])]
        if len(sec)!=1:raise ValueError("range crosses section/data or file boundary")
        spans.append((a,b,sec[0]))
    spans.sort(key=lambda x:x[0])
    if any(spans[i][0]<spans[i-1][1] for i in range(1,len(spans))):
        raise ValueError("overlapping code ranges")
    out=bytearray(data)
    for a,b,sec in spans:
        start=sec['offset']+a-sec['rva']
        end=sec['offset']+b-sec['rva']
        for i in range(start,end,2):out[i],out[i+1]=out[i+1],out[i]
    # Experimental NTOS private PE tag, not a standard SH4BE Windows CE machine type.
    struct.pack_into('<H',out,p['pe_offset']+4,NTOS_SH4BE_LEGACY)
    return bytes(out)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input',type=Path)
    ap.add_argument('--reference',type=Path)
    ap.add_argument('--report',type=Path)
    ap.add_argument('--code-ranges',type=Path,help="reviewed JSON list of [start_rva,end_rva]")
    ap.add_argument('--experimental-output',type=Path,
                    help="INCOMPLETE partial conversion, NOT guaranteed executable")
    args=ap.parse_args()
    src=args.input.read_bytes()
    report=json.dumps(inspect(src,args.reference.read_bytes() if args.reference else None),indent=2)+'\n'
    if args.report:args.report.write_text(report)
    else:print(report)
    if args.experimental_output:
        if not args.code_ranges:ap.error("refusing blind conversion: --code-ranges required")
        out=convert_manual(src,json.loads(args.code_ranges.read_text()))
        args.experimental_output.write_bytes(out)
        print("WARNING: incomplete conversion; FPU/literals/relocs/IAT/data not fixed")


if __name__=='__main__':main()
