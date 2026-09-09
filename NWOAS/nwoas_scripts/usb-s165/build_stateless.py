#!/usr/bin/env python3
"""Build S157 cursor-only comparison atop S163 50us, then restore source."""
from pathlib import Path
import subprocess,os,hashlib,json,difflib
R=Path('/Volumes/X31/NWOAS');repo=R/'m1n1_windows-s159';out=R/'nwoas_scripts/usb-s165'
p=repo/'src/hv_vm.c';before=p.read_text();other=(R/'nwoas_scripts/usb-s157/hv_vm-s157.c').read_text()
def bounds(s):
 a=s.index('static void nwoas_usbc_payload_alias(');b=s.index('\n/* D79 read-only descriptor capture',a);return a,b
a,b=bounds(before);x,y=bounds(other);candidate=before[:a]+other[x:y]+before[b:]
assert before!=candidate and 'saved_ctx' not in candidate[a:a+y-x]
(out/'hv_vm-s163-before.c').write_text(before);(out/'hv_vm-s165-stateless.c').write_text(candidate)
(out/'stateless-from-s163.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),candidate.splitlines(True),fromfile='a/src/hv_vm.c',tofile='b/src/hv_vm.c')))
try:
 p.write_text(candidate)
 env=os.environ.copy();env['RUSTUP_TOOLCHAIN']='1.88.0-aarch64-apple-darwin'
 subprocess.run(['nice','-n','10','make','-j2','EXTRA_CFLAGS=-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50','build/m1n1.bin'],cwd=repo,env=env,check=True)
 data=(repo/'build/m1n1.bin').read_bytes();target=out/'m1n1-s165-stateless-gap50.bin';target.write_bytes(data)
 (out/'stateless-manifest.json').write_text(json.dumps({'image':str(target),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'change':'S163 50us plus only USB-C S157 cursor persistence removal','hv_vm_sha256':hashlib.sha256(candidate.encode()).hexdigest()},indent=2)+'\n')
finally:
 if p.read_text()!=candidate:raise RuntimeError('Concurrent source edit: not overwriting')
 p.write_text(before)
