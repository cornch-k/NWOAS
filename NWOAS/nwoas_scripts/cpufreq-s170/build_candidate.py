#!/usr/bin/env python3
"""Build isolated ReadyToBoot P12 candidate on S139 layout; restore originals."""
from pathlib import Path
import subprocess,hashlib,json
R=Path('/Volumes/X31/NWOAS');repo=R/'apple_silicon_platforms_mu';out=R/'nwoas_scripts/cpufreq-s170';pkg='Silicon/Apple/AppleSiliconPkg/'
wrapper=repo/pkg/'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c';inf=wrapper.with_suffix('.inf');dsdt=repo/'Platform/MacMini2020Pkg/AcpiTables/DSDT.asl';header=repo/pkg/'Include/Library/NwoasCpuPstate.h'
assert not header.exists()
original={p:p.read_bytes() for p in [wrapper,inf,dsdt]};patched={}
for p,b in original.items():
 q=out/'source-before'/p.relative_to(repo);q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(b)
inner=(R/'nwoas_scripts/smp-s131/build_candidate.py').read_text().replace('OUT = ROOT / "nwoas_scripts/smp-s131"','OUT = ROOT / "nwoas_scripts/cpufreq-s170/s131-build"').replace('target = ROOT / "m1n1_windows/m1n1-payload-s131-8cpu.bin"','target = ROOT / "nwoas_scripts/cpufreq-s170/m1n1-payload-s170-p12-usba.bin"').replace('"TOOL_CHAIN_TAG=CLANGPDB",','"TOOL_CHAIN_TAG=CLANGPDB",\n                "MAX_CONCURRENT_THREAD_NUMBER=2",')
(out/'s131_builder.py').write_text(inner)
headerbytes=(out/'Include/Library/NwoasCpuPstate.h').read_bytes()
try:
 header.write_bytes(headerbytes)
 s=wrapper.read_text();s=s.replace('#include <Library/BaseLib.h>','#include <Library/BaseLib.h>\n#include <Library/IoLib.h>\n#include <Library/TimerLib.h>\n#include <Library/NwoasCpuPstate.h>')
 at=s.index('STATIC EFI_EVENT  mReadyToBootEvent');s=s[:at]+(out/'ready_to_boot.inc').read_text()+'\n'+s[at:]
 assert s.count('  mHideDone = TRUE;')==1;s=s.replace('  mHideDone = TRUE;','  mHideDone = TRUE;\n  NwoasPstateReadyToBoot();',1);wrapper.write_text(s)
 inf.write_text(inf.read_text().replace('  BaseLib\n','  BaseLib\n  BaseMemoryLib\n  IoLib\n  TimerLib\n'))
 s=dsdt.read_text();at=s.index('        Device (XHC1) {');pos=s.index('Return (0xF)',at);s=s[:pos]+s[pos:].replace('Return (0xF)','Return (Zero)',1);dsdt.write_text(s)
 patched={p:p.read_bytes() for p in original}
 subprocess.run(['nice','-n','10','python3',str(out/'s131_builder.py')],cwd=R,check=True)
 payload=out/'m1n1-payload-s170-p12-usba.bin';b=payload.read_bytes()
 m={'payload':str(payload),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'change':'S139 layout and cap plus ReadyToBoot P12; 8 cores USB-A; no late host P12 module','header_sha256':hashlib.sha256(headerbytes).hexdigest(),'source_sha256':{str(p.relative_to(repo)):hashlib.sha256(v).hexdigest() for p,v in patched.items()}}
 (out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m,indent=2))
finally:
 conflicts=[str(p) for p,b in patched.items() if p.read_bytes()!=b]
 for p,b in original.items():
  if str(p) not in conflicts:p.write_bytes(b)
 if header.exists() and header.read_bytes()==headerbytes:header.unlink()
 if conflicts:raise RuntimeError('Concurrent edits preserved: '+repr(conflicts))
