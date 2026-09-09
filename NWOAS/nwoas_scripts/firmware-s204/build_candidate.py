#!/usr/bin/env python3
"""S204: isolated UEFI Image-header correction candidate.

Derived from nwoas_scripts/firmware-s192/build_candidate.py.  Same RTC-lib DSC
swap and the full S192 source set, but every generated/output path is retargeted
to nwoas_scripts/firmware-s204 so no S192 artifact or manifest is touched, and
build_memory.py additionally applies the one-line MacMini2020.fdf image_size
correction (0x100E0000 -> 0x01E00000) with exact restore-in-finally.

Offline source build only.  No hardware, launcher, USB, guest/m1n1 main source,
secrets or UI is touched, and no ADT/NVRAM dump is read.  This records
before/after tracked hashes and preserves all pre-existing edits/submodules.
"""
from pathlib import Path
import subprocess, hashlib, json

R = Path('/Volumes/X31/NWOAS'); repo = R / 'apple_silicon_platforms_mu'
out = R / 'nwoas_scripts/firmware-s204'
dsc = repo / 'Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc'
folder = repo / 'Silicon/Apple/AppleSiliconPkg/Library/NwoasHardwareBootRtcLib'

# --- before/after tracked-state proof -------------------------------------
# Tracked files this workflow temporarily edits (DSC + FDF + every source file
# touched by build_memory.py and the generated s131_builder.py).  Restoration
# is proven by re-hashing these and re-reading git status after the build.
TRACKED = [
    'Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc',
    'Platform/MacMini2020Pkg/MacMini2020.fdf',
    'Silicon/Apple/AppleSiliconPkg/PrePi/AdtParser.c',
    'Silicon/Apple/AppleSiliconPkg/Drivers/AppleDartIoMmuDxe/AppleDartIoMmuDxe.c',
    'Silicon/Apple/AppleSiliconPkg/Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c',
    'Silicon/Apple/AppleSiliconPkg/Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.inf',
    'Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c',
    'Platform/MacMini2020Pkg/AcpiTables/DSDT.asl',
    'Silicon/Apple/AppleSiliconPkg/Library/DeviceBootManagerLib/DeviceBootManagerLib.c',
    'Silicon/Apple/AppleSiliconPkg/Library/DeviceBootManagerLib/DeviceBootManagerLib.inf',
    'MU_BASECORE/MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceIo.c',
    'Silicon/Apple/AppleSiliconPkg/Library/MsBootOptionsLib/MsBootOptionsLib.c',
    'Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc',
]


def tracked_hashes():
    h = {}
    for rel in TRACKED:
        p = repo / rel
        h[rel] = hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
    return h


def git_status():
    return subprocess.run(['git', '-c', 'core.fileMode=false', 'status', '--porcelain'],
                          cwd=repo, capture_output=True, text=True, check=True).stdout


before_hashes = tracked_hashes()
before_status = git_status()

assert not folder.exists()
original = dsc.read_bytes()
old = b'AppleSiliconPkg/Library/VirtualRealTimeClockLib/VirtualRealTimeClockLib.inf'
new = b'AppleSiliconPkg/Library/NwoasHardwareBootRtcLib/NwoasHardwareBootRtcLib.inf'
assert original.count(old) == 1
patched = original.replace(old, new); added = {}
(out / 'dsc-before.bin').write_bytes(original)
try:
    folder.mkdir()
    for p in (out / 'NwoasHardwareBootRtcLib').iterdir():
        if p.is_file(): added[folder / p.name] = p.read_bytes()
    for p, b in added.items(): p.write_bytes(b)
    dsc.write_bytes(patched)
    subprocess.run(['python3', str(out / 'build_memory.py')], check=True, cwd=R)
    m = json.loads((out / 'manifest.json').read_text())
    m['change'] = ('S204 = S192 (conformant RTC unseeded error + ReadyToBoot P12, S180 '
                   '10GiBhigh+low4GiB, 8 cores, USB-C XHC1 visible, direct SERA hardwareRTC '
                   'boot read with physicalcounter runtime advance, no host date seed, SetTime '
                   'unsupported) PLUS MacMini2020.fdf image_size corrected 0x100E0000->0x01E00000 '
                   '(30 MiB == PcdFdSize).  Source-built FD only; not a binary patch.')
    m['rtc_source_sha256'] = {p.name: hashlib.sha256(b).hexdigest() for p, b in added.items()}
    (out / 'manifest.json').write_text(json.dumps(m, indent=2) + '\n')
finally:
    if dsc.read_bytes() != patched: raise RuntimeError('Concurrent DSC edit preserved')
    dsc.write_bytes(original)
    for p, b in added.items():
        if p.read_bytes() == b: p.unlink()
        else: raise RuntimeError('Concurrent RTC source edit preserved')
    folder.rmdir()

after_hashes = tracked_hashes()
after_status = git_status()
restored = before_hashes == after_hashes and before_status == after_status
proof = {
    'restored': restored,
    'before_tracked_sha256': before_hashes,
    'after_tracked_sha256': after_hashes,
    'changed_after_restore': [k for k in before_hashes if before_hashes[k] != after_hashes[k]],
    'git_status_before': before_status,
    'git_status_after': after_status,
    'note': ('git_status carries only the pre-existing submodule modifications and untracked '
             'files; it must be byte-identical before and after.  All temporary edits (DSC, FDF '
             'and every source file) restore to their original tracked hash.'),
}
(out / 'restore-proof.json').write_text(json.dumps(proof, indent=2) + '\n')
print('RESTORE OK' if restored else 'RESTORE MISMATCH')
if not restored:
    raise SystemExit('tracked state not restored; see restore-proof.json')
