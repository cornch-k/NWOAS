from pathlib import Path
import subprocess,os,hashlib,json,difflib
R=Path('/Volumes/X31/NWOAS');repo=R/'m1n1_windows-s159';out=R/'nwoas_scripts/usb-s175'
vm=repo/'src/hv_vm.c';exc=repo/'src/hv_exc.c';make=repo/'Makefile';original={p:p.read_bytes() for p in [vm,exc,make]};added={}
for name in ['s175_dart1_translate.h','s175_dart1_translate.c']:
 p=repo/'src'/name;assert not p.exists();added[p]=(out/name).read_bytes()
s=vm.read_text();a=s.index('static void nwoas_usbc_payload_alias(');b=s.index('\n/* D79 read-only descriptor capture',a)
s=s[:a]+(out/'integration.inc').read_text()+s[b:]
needle='''                    if (paddr >= db+4 && paddr < db+128*4)
                        nwoas_usbc_payload_alias(op,(paddr-db)/4,val[0]&0xff);'''
assert needle in s;s=s.replace(needle,'''                    if (paddr == op && (val[0] & 1) && !(read32(op) & 1)) {
                        if (!nwoas_usbc_s175_start(op)) val[0] &= ~1ULL;
                    }''',1)
assert '#define NWOAS_DART_LOWALIAS 1' in s;s=s.replace('#define NWOAS_DART_LOWALIAS 1','#define NWOAS_DART_LOWALIAS 0',1)
assert '#define NWOAS_LOW_EVENT_SEED       0' in s
x=exc.read_text();needle='''        nwoas_usbc_event_coherence(rt_base, false);''';assert x.count(needle)==1
x=x.replace(needle,needle+'\n        extern void nwoas_usbc_s175_progress(u64 rt);\n        nwoas_usbc_s175_progress(rt_base);',1)
m=make.read_text();needle='hv.o hv_vm.o hv_exc.o';assert m.count(needle)==1;m=m.replace(needle,'hv.o hv_vm.o s175_dart1_translate.o hv_exc.o',1)
patched={vm:s.encode(),exc:x.encode(),make:m.encode()}
for p,b in original.items():
 q=out/'source-before'/p.relative_to(repo);q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(b)
for p,b in patched.items():
 q=out/'source-candidate'/p.relative_to(repo);q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(b)
(out/'target.patch').write_text(''.join(''.join(difflib.unified_diff(original[p].decode().splitlines(True),b.decode().splitlines(True),fromfile='a/'+str(p.relative_to(repo)),tofile='b/'+str(p.relative_to(repo)))) for p,b in patched.items()))
try:
 for p,b in {**patched,**added}.items():p.write_bytes(b)
 env=os.environ.copy();env['RUSTUP_TOOLCHAIN']='1.88.0-aarch64-apple-darwin'
 subprocess.run(['nice','-n','10','make','-j2','EXTRA_CFLAGS=-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50','build/m1n1.bin'],cwd=repo,env=env,check=True)
 b=(repo/'build/m1n1.bin').read_bytes();target=out/'m1n1-s175-dart1-gap50.bin';target.write_bytes(b)
 manifest={'image':str(target),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'change':'S163 gap50 plus USB-C DART1/SID1 translation preserving DAPF, no D83 rewrites or LOWALIAS; event seed remains0','source_sha256':{str(p.relative_to(repo)):hashlib.sha256(b).hexdigest() for p,b in {**patched,**added}.items()}}
 (out/'hardware-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
finally:
 conflicts=[str(p) for p,b in patched.items() if p.read_bytes()!=b]
 for p,b in original.items():
  if str(p) not in conflicts:p.write_bytes(b)
 for p,b in added.items():
  if p.exists() and p.read_bytes()==b:p.unlink()
 if conflicts:raise RuntimeError('Concurrent edits preserved: '+repr(conflicts))
