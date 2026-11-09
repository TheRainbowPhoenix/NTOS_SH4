"""Collect actual MSVC/Renesas compiler listings; needs no linker or emulator."""
import os,subprocess,json,hashlib,argparse
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--gcc',action='store_true');args=ap.parse_args()
R=Path(__file__).resolve().parents[2];S=Path(__file__).with_name('compare.c');O=R/'obj_ce_abi/compare';O.mkdir(parents=True,exist_ok=True)
B=Path(r'C:\Users\Phoebe\Downloads\SHC_SH_Compiler');M=B/'MSVC/CEPB/CEPB211/BIN/shcl.exe';H=B/'9_4_3/bin/shc.exe'
env=os.environ.copy();env['SHC_LIB']=str(H.parent);env['SHC_INC']=str(B/'9_4_3/include')
commands=[]
def run(args):
 commands.append([str(a) for a in args]);subprocess.run(commands[-1],env=env,check=True,cwd=R)
run([M,'/nologo','/c','/Qsh4','/DSHx','/O2','/FAcs','/Fa'+str(O/'msvc.asm'),'/Fo'+str(O/'msvc.obj'),'/I'+str(B/'MSVC/CEPB/WCE211/MAXALL/INCLUDE'),S])
for suffix,cpu,endian in [('le','sh4','little'),('be','sh4','big'),('nofpu-be','sh4aldsp','big')]:
 run([H,S,'-cpu='+cpu,'-endian='+endian,'-optimize=1','-code=asmcode','-include='+str(B/'9_4_3/include'),'-objectfile='+str(O/('renesas-'+suffix+'.src'))])
if args.gcc:
 for suffix,define in [('native',''),('renesas','-DABI_GCC_RENESAS=1')]:
  run(['docker','run','--rm','-v',str(R)+':/work','-w','/work','ghcr.io/classpaddev/hollyhock-3:v2.2.1','sh','-c','/toolchain/bin/sh4a_nofpueb-elf-gcc -S -Os -ffreestanding -fno-builtin '+define+' payload/abi_compare/compare.c -o obj_ce_abi/compare/gcc-'+suffix+'.s'])
(O/'commands.json').write_text(json.dumps({'commands':commands,'source_sha256':hashlib.sha256(S.read_bytes()).hexdigest(),'compilers':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [M,H]}},indent=2))
print('Assembly evidence:',O)

