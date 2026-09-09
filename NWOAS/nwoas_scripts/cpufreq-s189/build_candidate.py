from pathlib import Path
import subprocess,hashlib,json
R=Path('/Volumes/X31/NWOAS');repo=R/'apple_silicon_platforms_mu';out=R/'nwoas_scripts/cpufreq-s189'
dsc=repo/'Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc'
folder=repo/'Silicon/Apple/AppleSiliconPkg/Library/NwoasHardwareBootRtcLib'
assert not folder.exists()
original=dsc.read_bytes();old=b'AppleSiliconPkg/Library/VirtualRealTimeClockLib/VirtualRealTimeClockLib.inf'
new=b'AppleSiliconPkg/Library/NwoasHardwareBootRtcLib/NwoasHardwareBootRtcLib.inf'
assert original.count(old)==1
patched=original.replace(old,new);added={}
(out/'dsc-before.bin').write_bytes(original)
try:
 folder.mkdir()
 for p in (out/'NwoasHardwareBootRtcLib').iterdir():
  if p.is_file():added[folder/p.name]=p.read_bytes()
 for p,b in added.items():p.write_bytes(b)
 dsc.write_bytes(patched)
 subprocess.run(['python3',str(out/'build_memory.py')],check=True,cwd=R)
 m=json.loads((out/'manifest.json').read_text());m['change']='S189 ReadyToBoot P12 plus S180 10GiBhigh+low4GiB,8cores,USB-C XHC1 visible; direct SERA hardwareRTC boot read with physicalcounter runtime advance; no host date seed, SetTime unsupported'
 m['rtc_source_sha256']={p.name:hashlib.sha256(b).hexdigest() for p,b in added.items()}
 (out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
finally:
 if dsc.read_bytes()!=patched:raise RuntimeError('Concurrent DSC edit preserved')
 dsc.write_bytes(original)
 for p,b in added.items():
  if p.read_bytes()==b:p.unlink()
  else:raise RuntimeError('Concurrent RTC source edit preserved')
 folder.rmdir()
