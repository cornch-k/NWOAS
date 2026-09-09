#!/usr/bin/env python3
"""Export tested source snapshots through temporary git indexes. No checkout edits."""
from pathlib import Path
import os, subprocess, tempfile, hashlib, json
ROOT=Path('/Volumes/X31/NWOAS')
OUT=ROOT/'nwoas_scripts/publish-s217/companion'
OUT.mkdir(exist_ok=True)
S192=ROOT/'nwoas_scripts/firmware-s216'

def run(repo,args,*,env=None,data=None):
 return subprocess.check_output(['git','-C',str(repo),*args],env=env,input=data)

def export(name,repo,replacements):
 base=run(repo,['rev-parse','HEAD']).decode().strip()
 with tempfile.TemporaryDirectory() as td:
  env=os.environ.copy();env['GIT_INDEX_FILE']=str(Path(td)/'index')
  run(repo,['read-tree',base],env=env)
  diff=run(repo,['diff','--binary','--ignore-submodules=all',base])
  if diff:run(repo,['apply','--cached','--whitespace=nowarn','-'],env=env,data=diff)
  for rel,source in replacements.items():
   data=source.read_bytes();oid=run(repo,['hash-object','-w','--stdin'],data=data).decode().strip()
   entries=run(repo,['ls-files','--stage','--',rel],env=env).decode().splitlines()
   mode=entries[0].split()[0] if entries else ('100755' if source.stat().st_mode & 0o111 else '100644')
   run(repo,['update-index','--add','--cacheinfo',f'{mode},{oid},{rel}'],env=env)
  patch=run(repo,['diff','--cached','--binary',base],env=env)
  names=run(repo,['diff','--cached','--name-only',base],env=env).decode().splitlines()
  run(repo,['read-tree',base],env=env)
  run(repo,['apply','--cached','--check','--whitespace=nowarn','-'],env=env,data=patch)
 (OUT/(name+'.patch')).write_bytes(patch)
 return {'component':name,'base_commit':base,'patch':name+'.patch','sha256':hashlib.sha256(patch).hexdigest(),'files':names,'apply_check':'PASS against clean temporary index'}

pkg='Silicon/Apple/AppleSiliconPkg/'
uefi={str(p.relative_to(S192/'source-candidate')):p for p in (S192/'source-candidate').rglob('*') if p.is_file()}
for rel in [pkg+'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c',pkg+'Library/DeviceBootManagerLib/DeviceBootManagerLib.c',pkg+'Library/DeviceBootManagerLib/DeviceBootManagerLib.inf',pkg+'Library/MsBootOptionsLib/MsBootOptionsLib.c','Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc']:
 uefi[rel]=S192/'s131-build'/(Path(rel).name+'.candidate')
for p in (S192/'NwoasHardwareBootRtcLib').iterdir():
 if p.is_file():uefi[pkg+'Library/NwoasHardwareBootRtcLib/'+p.name]=p
for p in (S192/'Include/Library').iterdir():
 if p.is_file():uefi[pkg+'Include/Library/'+p.name]=p
uefi[pkg+'Include/Library/NwoasGuestRam.h']=ROOT/'nwoas_scripts/memory-s161/Include/Library/NwoasGuestRam.h'
dsc=(S192/'dsc-before.bin').read_bytes();old=b'AppleSiliconPkg/Library/VirtualRealTimeClockLib/VirtualRealTimeClockLib.inf';new=b'AppleSiliconPkg/Library/NwoasHardwareBootRtcLib/NwoasHardwareBootRtcLib.inf'
assert dsc.count(old)==1
(OUT/'AppleSiliconPkg.dsc.inc').write_bytes(dsc.replace(old,new))
uefi[pkg+'AppleSiliconPkg.dsc.inc']=OUT/'AppleSiliconPkg.dsc.inc'
uefi[pkg+'Include/Library/NwoasHandoffRanges.h']=S192/'NwoasHandoffRanges.h'
uefi['Platform/MacMini2020Pkg/MacMini2020.fdf']=S192/'MacMini2020.fdf.candidate'
entries=[export('m1n1_windows',ROOT/'m1n1_windows-s159',{'proxyclient/tools/run_guest.py':ROOT/'m1n1_windows/proxyclient/tools/run_guest.py', 'src/hv_exc.c':ROOT/'nwoas_scripts/nvme-s208/hv_exc-s208.c', 'src/hv_vm.c':ROOT/'nwoas_scripts/nvme-s208/hv_vm-s208.c'}),export('apple_silicon_platforms_mu',ROOT/'apple_silicon_platforms_mu',uefi),export('MU_BASECORE',ROOT/'apple_silicon_platforms_mu/MU_BASECORE',{'MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceIo.c':S192/'s131-build/NonDiscoverablePciDeviceIo.c.candidate'}),export('ARM_TIANO',ROOT/'apple_silicon_platforms_mu/Silicon/ARM/TIANO',{})]
def export_guest():
 repo=ROOT/'m1n1-guest-s215'; base=run(repo,['rev-parse','HEAD']).decode().strip()
 with tempfile.TemporaryDirectory() as td:
  env=os.environ.copy();env['GIT_INDEX_FILE']=str(Path(td)/'index')
  run(repo,['read-tree',base],env=env)
  for patch in [ROOT/'nwoas_scripts/loader-s197/guest-compat-s197.patch',ROOT/'nwoas_scripts/loader-s215/placement.patch']:
   run(repo,['apply','--cached','--whitespace=nowarn','-'],env=env,data=patch.read_bytes())
  h=(ROOT/'nwoas_scripts/loader-s215/handoff_layout.h').read_bytes()
  oid=run(repo,['hash-object','-w','--stdin'],data=h).decode().strip()
  run(repo,['update-index','--add','--cacheinfo',f'100644,{oid},src/nwoas_handoff_layout.h'],env=env)
  data=run(repo,['diff','--cached','--binary',base],env=env)
  names=run(repo,['diff','--cached','--name-only',base],env=env).decode().splitlines()
  run(repo,['read-tree',base],env=env)
  run(repo,['apply','--cached','--check','--whitespace=nowarn','-'],env=env,data=data)
 (OUT/'m1n1_guest.patch').write_bytes(data)
 return {'component':'m1n1_guest','base_commit':base,'patch':'m1n1_guest.patch','sha256':hashlib.sha256(data).hexdigest(),'files':names,'apply_check':'PASS against clean temporary index'}
entries.append(export_guest())
oldmeta=json.loads((ROOT/'nwoas_scripts/publish-s198/companion/manifest.json').read_text())
metadata={'source':'S216 candidate UEFI, S215 dedicated guest prefix and S208 diagnostic HV; runtime launchers separate',
          'hardware_status':'PENDING S215/S216; see final result evidence before adoption',
          'hv_build_flags':'-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50',
          'guest_build_tag':'v1.0.2-1474-gbddf7f06-s215handoff','components':entries,
          'uefi_payload_sha256':'0ae1cb75bfc3cbc2b7b15e6408c489fe5badd3d12d53fc4353a85b6b5ad3d197',
          'guest_prefix_sha256':'d91700ccbe277461513ca9d8320263bf41fc33da8cb814ba095d0f857d02e334',
          'note':'S198 remains unchanged for historical S205 reproduction; this export preserves run_guest.py executable mode.'}
for k,v in oldmeta.items():
 if 'submodule' in k:metadata[k]=v
# Retain separately collected evidence only for an identical exported build.
previous_path=OUT/'manifest.json'
if previous_path.exists():
 previous=json.loads(previous_path.read_text())
 def identity(m):
  return (m.get('uefi_payload_sha256'),m.get('guest_prefix_sha256'),
          [(e['component'],e['base_commit'],e['sha256']) for e in m.get('components',[])])
 if identity(previous)==identity(metadata):
  for key,value in previous.items():
   if key not in metadata or key=='hardware_status':metadata[key]=value
(OUT/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
for e in entries:print(e['component'],len(e['files']),e['apply_check'])
