#!/usr/bin/env python3
"""Build isolated S172 8-GiB high-RAM output; temporarily edit and exactly restore UEFI sources.
Does not touch disks, guest state, or existing S131/S139 payloads."""
from pathlib import Path
import subprocess,hashlib,json,os
ROOT=Path('/Volumes/X31/NWOAS')
REPO=ROOT/'apple_silicon_platforms_mu'
OUT=ROOT/'nwoas_scripts/rescue-s179'
package='Silicon/Apple/AppleSiliconPkg/'
rels=[package+'PrePi/AdtParser.c',package+'Drivers/AppleDartIoMmuDxe/AppleDartIoMmuDxe.c',
      package+'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c',
      'Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c',
      package+'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.inf',
      'Platform/MacMini2020Pkg/AcpiTables/DSDT.asl']
header=REPO/package/'Include/Library/NwoasGuestRam.h'
split_header=REPO/package/'Include/Library/NwoasSplitMemoryMap.h'
assert not split_header.exists()
assert not header.exists(), 'Refuse overwrite of existing shared header'
original={REPO/r:(REPO/r).read_bytes() for r in rels}
snap=OUT/'source-before';snap.mkdir(exist_ok=True)
for p,b in original.items():
    q=snap/p.relative_to(REPO);q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(b)
inner=(ROOT/'nwoas_scripts/smp-s131/build_candidate.py').read_text()
inner=inner.replace('OUT = ROOT / "nwoas_scripts/smp-s131"','OUT = ROOT / "nwoas_scripts/rescue-s179/s131-build"')
inner=inner.replace('target = ROOT / "m1n1_windows/m1n1-payload-s131-8cpu.bin"',
                    'target = ROOT / "nwoas_scripts/rescue-s179/m1n1-payload-s179-usb-first.bin"')
inner=inner.replace('"TOOL_CHAIN_TAG=CLANGPDB",','"TOOL_CHAIN_TAG=CLANGPDB",\n                "MAX_CONCURRENT_THREAD_NUMBER=2",')
inner=inner.replace(' + ssd + "\\n" + usb,', ' + usb + "\\n" + ssd,').replace('S131 internal SSD first', 'S179 USB first rescue')
(OUT/'s131_builder.py').write_text(inner)
patched={}
try:
    subprocess.run(['git','apply','--check',str(ROOT/'nwoas_scripts/memory-s161/nwoas-s161-guest-ram-normalize.diff')],cwd=REPO,check=True)
    subprocess.run(['git','apply',str(ROOT/'nwoas_scripts/memory-s161/nwoas-s161-guest-ram-normalize.diff')],cwd=REPO,check=True)
    header.write_bytes((ROOT/'nwoas_scripts/memory-s161/Include/Library/NwoasGuestRam.h').read_bytes())
    split_header.write_bytes((OUT/'Include/Library/NwoasSplitMemoryMap.h').read_bytes())
    wrapper=REPO/package/'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c'
    wrapper.write_text('#define NWOAS_HIDE_KEEP_HIGH_BYTES 0x200000000ULL\n'+(OUT/'NwoasHideHighRamDxe-s167.c').read_text())
    inf=REPO/package/'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.inf'
    inf.write_text(inf.read_text().replace('  BaseLib\n','  BaseLib\n  BaseMemoryLib\n'))
    dsdt=REPO/rels[-1];s=dsdt.read_bytes()
    device=b'        Device (XHC1) {'
    start=s.index(device);pos=s.index(b'Return (0xF)',start)
    assert pos<s.index(b'Device (',start+len(device)) if b'Device (' in s[start+len(device):] else True
    s=s[:pos]+s[pos:].replace(b'Return (0xF)',b'Return (Zero)',1);dsdt.write_bytes(s)
    patched={p:p.read_bytes() for p in original}
    subprocess.run(['nice','-n','10','python3',str(OUT/'s131_builder.py')],cwd=ROOT,check=True)
    payload=OUT/'m1n1-payload-s179-usb-first.bin'
    manifest={'payload':str(payload),'bytes':payload.stat().st_size,
              'sha256':hashlib.sha256(payload.read_bytes()).hexdigest(),
              'change':'S172 validated memory/8cores/USB-A plus USB-first boot order for WinPE recovery; no automatic install',
              'source_sha256':{str(p.relative_to(REPO)):hashlib.sha256(b).hexdigest() for p,b in patched.items()}}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))
finally:
    # If another writer changed a patched file, retain it and report rather than clobber.
    conflicts=[str(p) for p,b in patched.items() if p.read_bytes()!=b]
    for p,b in original.items():
        if str(p) not in conflicts:
            p.write_bytes(b)
    if header.exists() and header.read_bytes()==(ROOT/'nwoas_scripts/memory-s161/Include/Library/NwoasGuestRam.h').read_bytes():
        header.unlink()
    if split_header.exists() and split_header.read_bytes()==(OUT/'Include/Library/NwoasSplitMemoryMap.h').read_bytes():
        split_header.unlink()
    if conflicts:
        raise RuntimeError('Concurrent edits preserved: '+repr(conflicts))
