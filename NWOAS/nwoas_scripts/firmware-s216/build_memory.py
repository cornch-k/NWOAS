#!/usr/bin/env python3
"""S216 inner builder: temporarily edit and exactly restore UEFI sources, build
the FD with the S192 source set plus the one MacMini2020.fdf image_size fix.

Derived from nwoas_scripts/firmware-s192/build_memory.py.  Retargets all
generated/output paths to nwoas_scripts/firmware-s216, uses nice -n 19 / -j2,
and adds the FDF image_size correction (0x00,0x00,0x0e,0x10 -> 0x00,0x00,0xe0,
0x01) around the whole build, restoring the FDF to its exact original bytes even
on failure.  Does not touch disks, guest state, launchers, or S192 artifacts.
"""
from pathlib import Path
import subprocess, hashlib, json, os, difflib
ROOT = Path('/Volumes/X31/NWOAS')
REPO = ROOT / 'apple_silicon_platforms_mu'
OUT = ROOT / 'nwoas_scripts/firmware-s216'
VARIANT = os.environ.get('NWOAS_GUEST_VARIANT', 'compat')
assert VARIANT == 'compat'
subprocess.run(['python3', str(ROOT / 'nwoas_scripts/dtb-s221/build.py')], check=True)
PAY = 'm1n1-payload-s216-' + VARIANT + '.bin'
package = 'Silicon/Apple/AppleSiliconPkg/'
rels = ['Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.inf', package + 'PrePi/AdtParser.c', package + 'Drivers/AppleDartIoMmuDxe/AppleDartIoMmuDxe.c',
        package + 'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c',
        'Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c',
        package + 'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.inf',
        'Platform/MacMini2020Pkg/AcpiTables/DSDT.asl']
header = REPO / package / 'Include/Library/NwoasGuestRam.h'
split_header = REPO / package / 'Include/Library/NwoasSplitMemoryMap.h'
pstate_header = REPO / package / 'Include/Library/NwoasCpuPstate.h'

# --- FDF image_size correction (source-only, exact needle) -----------------
FDF = REPO / 'Platform/MacMini2020Pkg/MacMini2020.fdf'
FDF_OLD = b'0x00, 0x00, 0x0e, 0x10, 0x00, 0x00, 0x00, 0x00, # image_size: 30 MB'
FDF_NEW = b'0x00, 0x00, 0xe0, 0x01, 0x00, 0x00, 0x00, 0x00, # image_size: 30 MB (0x01E00000 == PcdFdSize)'
fdf_original = FDF.read_bytes()
assert fdf_original.count(FDF_OLD) == 1, 'MacMini2020.fdf image_size needle not unique'
fdf_patched = fdf_original.replace(FDF_OLD, FDF_NEW, 1)
(OUT / 'MacMini2020.fdf.before').write_bytes(fdf_original)
(OUT / 'MacMini2020.fdf.candidate').write_bytes(fdf_patched)
(OUT / 'image_size.patch').write_text(''.join(difflib.unified_diff(
    fdf_original.decode().splitlines(keepends=True), fdf_patched.decode().splitlines(keepends=True),
    fromfile='a/Platform/MacMini2020Pkg/MacMini2020.fdf',
    tofile='b/Platform/MacMini2020Pkg/MacMini2020.fdf')))


assert not pstate_header.exists()
assert not split_header.exists()
assert not header.exists(), 'Refuse overwrite of existing shared header'
original = {REPO / r: (REPO / r).read_bytes() for r in rels}
snap = OUT / 'source-before'; snap.mkdir(exist_ok=True)
for p, b in original.items():
    q = snap / p.relative_to(REPO); q.parent.mkdir(parents=True, exist_ok=True); q.write_bytes(b)
inner = (ROOT / 'nwoas_scripts/smp-s131/build_candidate.py').read_text()
inner = inner.replace('OUT = ROOT / "nwoas_scripts/smp-s131"', 'OUT = ROOT / "nwoas_scripts/firmware-s216/s131-build"')
inner = inner.replace('target = ROOT / "m1n1_windows/m1n1-payload-s131-8cpu.bin"',
                      'target = ROOT / "nwoas_scripts/firmware-s216/' + PAY + '"')
inner = inner.replace('"TOOL_CHAIN_TAG=CLANGPDB",', '"TOOL_CHAIN_TAG=CLANGPDB",\n                "MAX_CONCURRENT_THREAD_NUMBER=2",')
ASSEMBLY_OLD = '    old = (ROOT / "m1n1_windows/m1n1-payload-s129-ssd-first.bin").read_bytes()\n    assert len(old) == 32342016 and len(fd) == len(old) - 1376256\n    payload = old[:1376256] + fd'
ASSEMBLY_NEW = "    variant = os.environ.get('NWOAS_GUEST_VARIANT', 'compat')\n    expected = {'compat':'d91700ccbe277461513ca9d8320263bf41fc33da8cb814ba095d0f857d02e334', 'head':'b2515b77d5e3cc5d5031823ce88bd0b25fdf1f4c41a9183ce92ef12c29a0b007'}\n    assert variant == 'compat'\n    prefix = (ROOT / 'nwoas_scripts/loader-s215/out/m1n1.bin').read_bytes()\n    dtb = (ROOT / 'nwoas_scripts/dtb-s221/out/apple-j274-padded.dtb').read_bytes()\n    assert len(prefix) == 0x20c000 and sha256(prefix) == expected[variant]\n    assert len(dtb) == 0x10000 and sha256(dtb) == 'ecc93b24740d80007d31986a43983c36eef47dd8b0b105b663c25a7b51190f76'\n    assert len(fd) == 0x1d88000 and int.from_bytes(fd[16:24], 'little') == 0x1e00000\n    payload = prefix + dtb + fd\n    (OUT.parent / 'source-built.fd').write_bytes(fd)\n    (OUT.parent / ('components-' + variant + '.json')).write_text(json.dumps({'variant':variant, 'prefix_sha256':sha256(prefix), 'dtb_sha256':sha256(dtb), 'fd_sha256':sha256(fd), 'payload_sha256':sha256(payload), 'bytes':len(payload), 'opaque_prefix_used':False}, indent=2) + '\\n')"
assert inner.count(ASSEMBLY_OLD) == 1
inner = inner.replace(ASSEMBLY_OLD, ASSEMBLY_NEW)
assert 'm1n1-payload-s129' not in inner
compile(inner, str(OUT / 's131_builder.py'), 'exec')
(OUT / 's131_builder.py').write_text(inner)
patched = {}
fdf_applied = False
try:
    # FDF correction first so it is present for the whole GenFds run.
    FDF.write_bytes(fdf_patched); fdf_applied = True
    pstate_header.write_bytes((OUT / 'Include/Library/NwoasCpuPstate.h').read_bytes())
    subprocess.run(['git', 'apply', '--check', str(ROOT / 'nwoas_scripts/memory-s161/nwoas-s161-guest-ram-normalize.diff')], cwd=REPO, check=True)
    subprocess.run(['git', 'apply', str(ROOT / 'nwoas_scripts/memory-s161/nwoas-s161-guest-ram-normalize.diff')], cwd=REPO, check=True)
    header.write_bytes((ROOT / 'nwoas_scripts/memory-s161/Include/Library/NwoasGuestRam.h').read_bytes())
    split_header.write_bytes((OUT / 'Include/Library/NwoasSplitMemoryMap.h').read_bytes())
    wrapper = REPO / package / 'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c'
    wrapper.write_text('#define NWOAS_HIDE_KEEP_HIGH_BYTES 0x280000000ULL\n' + (OUT / 'NwoasHideHighRamDxe-s167.c').read_text())
    inf = REPO / package / 'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.inf'
    inf.write_text(inf.read_text().replace('  BaseLib\n', '  BaseLib\n  BaseMemoryLib\n  IoLib\n  TimerLib\n'))
    from patch_sources import patch
    patch(REPO, OUT)
    # USB-C XHC1 remains visible in original DSDT.
    patched = {p: p.read_bytes() for p in original}
    for p, b in patched.items():
        q = OUT / 'source-candidate' / p.relative_to(REPO)
        q.parent.mkdir(parents=True, exist_ok=True); q.write_bytes(b)
    subprocess.run(['nice', '-n', '19', 'python3', str(OUT / 's131_builder.py')], cwd=ROOT, check=True)
    payload = OUT / PAY
    manifest = {'payload': str(payload), 'bytes': payload.stat().st_size,
                'sha256': hashlib.sha256(payload.read_bytes()).hexdigest(),
                'change': 'S161 normalization plus first 10 GiB high RAM kept; 8 CPUs; XHC1 visible; FDF image_size = 0x01E00000 (30 MiB)',
                'fdf_image_size': {'before': FDF_OLD.decode(), 'after': FDF_NEW.decode(),
                                   'fdf_before_sha256': hashlib.sha256(fdf_original).hexdigest(),
                                   'fdf_after_sha256': hashlib.sha256(fdf_patched).hexdigest()},
                'source_sha256': {str(p.relative_to(REPO)): hashlib.sha256(b).hexdigest() for p, b in patched.items()}}
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))
finally:
    # Restore the FDF to its exact original bytes even on failure; refuse to
    # clobber a concurrent writer.
    fdf_conflict = fdf_applied and FDF.read_bytes() != fdf_patched
    if fdf_applied and not fdf_conflict:
        FDF.write_bytes(fdf_original)
    # If another writer changed a patched source file, retain it and report.
    conflicts = [str(p) for p, b in patched.items() if p.read_bytes() != b]
    for p, b in original.items():
        if str(p) not in conflicts:
            p.write_bytes(b)
    if header.exists() and header.read_bytes() == (ROOT / 'nwoas_scripts/memory-s161/Include/Library/NwoasGuestRam.h').read_bytes():
        header.unlink()
    if split_header.exists() and split_header.read_bytes() == (OUT / 'Include/Library/NwoasSplitMemoryMap.h').read_bytes():
        split_header.unlink()
    if pstate_header.exists() and pstate_header.read_bytes() == (OUT / 'Include/Library/NwoasCpuPstate.h').read_bytes():
        pstate_header.unlink()
    handoff_header = REPO / package / 'Include/Library/NwoasHandoffRanges.h'
    if handoff_header.exists() and handoff_header.read_bytes() == (OUT / 'NwoasHandoffRanges.h').read_bytes():
        handoff_header.unlink()
    if fdf_conflict:
        raise RuntimeError('Concurrent MacMini2020.fdf edit preserved; original not restored by S216')
    if conflicts:
        raise RuntimeError('Concurrent edits preserved: ' + repr(conflicts))
    if fdf_applied and FDF.read_bytes() != fdf_original:
        raise RuntimeError('MacMini2020.fdf not restored to original')
