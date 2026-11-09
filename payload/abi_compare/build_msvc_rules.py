"""Compile the exact supplied probe and its legacy-compatible no-inline variant."""
import hashlib,json,os,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[2];H=Path(__file__).parent;O=R/'obj_ce_abi/msvc-rules';O.mkdir(parents=True,exist_ok=True)
B=Path(r'C:\Users\Phoebe\Downloads\SHC_SH_Compiler\MSVC\CEPB');C=B/'CEPB211/BIN/shcl.exe';records=[]
env=os.environ.copy();env.pop('CL',None);env.pop('_CL_',None)
def compile_(name,source,flags,expected=0):
 command=[str(C),'/nologo','/c',*flags,'/FA','/Fa'+str(O/(name+'.asm')),'/Fo'+str(O/(name+'.obj')),str(source)]
 p=subprocess.run(command,cwd=R,env=env,capture_output=True,text=True,errors='replace')
 (O/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
 records.append({'name':name,'command':command,'return_code':p.returncode,'expected_success':expected==0})
 if expected and not ('C2485' in p.stdout+p.stderr and 'noinline' in p.stdout+p.stderr):raise RuntimeError(name+': failure was not the expected unsupported noinline attribute')
 if (p.returncode==0)!=(expected==0):raise RuntimeError(name+': unexpected compiler result; inspect log')
 print(name,':',p.returncode)
for opt in ['Od','O2']:compile_('exact-'+opt,H/'msvc_abi_probe.c',['/'+opt],expected=1)
source=(H/'msvc_abi_probe.c').read_text(encoding='utf-8')
compatible=O/'probe-compatible.c';compatible.write_text(source.replace('__declspec(noinline) ',''),encoding='utf-8')
for target in ['default','sh4']:
 for opt in ['Od','O2']:
  compile_(target+'-'+opt,compatible,(['/Qsh4'] if target=='sh4' else [])+['/'+opt,'/Ob0'])
for opt in ['Od','O2']:
 compile_('extended-'+opt,H/'msvc_abi_extended.c',['/Qsh4','/DSHx','/'+opt,'/Ob0','/I'+str(B/'WCE211/MAXALL/INCLUDE')])
(O/'build.json').write_text(json.dumps({'compiler':'MSVC SH 12.00.8281.0','commands':records,'sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [C,C.with_name('c1_sh.dll'),C.with_name('c2.dll'),H/'msvc_abi_probe.c',H/'msvc_abi_extended.c']}},indent=2),encoding='utf-8')
