#!/usr/bin/env python3
"""S237: rebuild the S223 guest payload from source in an ISOLATED Mu tree copy.

  --variant repro : S216 source state exactly (reproduction of S216 FD / S223 payload)
  --variant s237  : S216 source state + PCIe bridge-window reservation in MemoryPeim

The tree passed with --tree must be a fresh APFS clone of apple_silicon_platforms_mu
(this script refuses a tree whose tracked state differs from the recorded S216 "before"
state). Edits stay in that clone; apple_silicon_platforms_mu and every existing
nwoas_scripts/* directory are only read. No device, launcher or installed image is touched.

The S216 edit sequence is re-executed in the same order as firmware-s216/build_candidate.py
-> build_memory.py -> patch_sources.patch -> s131_builder.py, and every resulting file is
checked against the bytes recorded by S216 (source-candidate/, s131-build/*.candidate,
MacMini2020.fdf.candidate, manifest.json source_sha256).

--logical-root: run stuart with the workspace reached through this path (a symlink to the
clone's parent). Needed for byte identity, because DEBUG/CLANGPDB images embed absolute
source and PDB paths, and S216 was built at /Volumes/X31/NWOAS/apple_silicon_platforms_mu.
"""
from pathlib import Path
import argparse, difflib, hashlib, json, os, subprocess, sys
sys.dont_write_bytecode = True  # never write __pycache__ into firmware-s216

ROOT = Path('/Users/angigyeom/Desktop/DEV/NWOAS')
S = ROOT / 'nwoas_scripts'
F216 = S / 'firmware-s216'
HERE = S / 'fw-s237'
TOOLBIN = HERE / 'toolbin'
VENV = ROOT / 'fw-s237-mu/venv'
PKG = 'Silicon/Apple/AppleSiliconPkg/'
MEMC = 'Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c'
MEMINF = 'Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.inf'


def sha(b):
    return hashlib.sha256(b).hexdigest()


ap = argparse.ArgumentParser()
ap.add_argument('--tree', required=True)
ap.add_argument('--variant', choices=['repro', 's237'], required=True)
ap.add_argument('--logical-root', default=None,
                help='symlinked path whose apple_silicon_platforms_mu entry resolves to --tree')
ap.add_argument('--out', required=True)
ap.add_argument('--jobs', default='2')
ap.add_argument('--rebuild-only', action='store_true',
                help='repeat the build in a tree this script already edited (its Build dir moved away first)')
a = ap.parse_args()
TREE = Path(a.tree).resolve()
OUT = Path(a.out); OUT.mkdir(parents=True, exist_ok=True)
assert TREE != (ROOT / 'apple_silicon_platforms_mu').resolve(), 'refuse to edit the source of record'
assert not (TREE / 'Build').exists(), 'tree already built; use a fresh clone'

# ---------------------------------------------------------------- pristine check
exec_edits = not a.rebuild_only
if exec_edits:
    proof = json.loads((F216 / 'restore-proof.json').read_text())
    for rel, h in proof['before_tracked_sha256'].items():
        assert sha((TREE / rel).read_bytes()) == h, ('tree not in S216 before-state', rel)
    st = subprocess.run(['git', '-c', 'core.fileMode=false', 'status', '--porcelain'], cwd=TREE,
                        capture_output=True, text=True, check=True).stdout
    assert st == proof['git_status_before'].replace('?? venv/\n', ''), st

    # ---------------------------------------------------------------- S216 edit sequence
    # (1) build_candidate.py: RTC library swap in the DSC + new library folder
    dsc = TREE / PKG / 'AppleSiliconPkg.dsc.inc'
    old = b'AppleSiliconPkg/Library/VirtualRealTimeClockLib/VirtualRealTimeClockLib.inf'
    new = b'AppleSiliconPkg/Library/NwoasHardwareBootRtcLib/NwoasHardwareBootRtcLib.inf'
    d = dsc.read_bytes(); assert d.count(old) == 1 and d == (F216 / 'dsc-before.bin').read_bytes()
    dsc.write_bytes(d.replace(old, new))
    rtc = TREE / PKG / 'Library/NwoasHardwareBootRtcLib'; rtc.mkdir()
    m216 = json.loads((F216 / 'manifest.json').read_text())
    for p in (F216 / 'NwoasHardwareBootRtcLib').iterdir():
        if p.is_file():
            b = p.read_bytes(); assert sha(b) == m216['rtc_source_sha256'][p.name], p
            (rtc / p.name).write_bytes(b)
    # (2) build_memory.py: FDF image_size, headers, S161 diff, S167 HideHighRam, INF libs
    fdf = TREE / 'Platform/MacMini2020Pkg/MacMini2020.fdf'
    FDF_OLD = b'0x00, 0x00, 0x0e, 0x10, 0x00, 0x00, 0x00, 0x00, # image_size: 30 MB'
    FDF_NEW = b'0x00, 0x00, 0xe0, 0x01, 0x00, 0x00, 0x00, 0x00, # image_size: 30 MB (0x01E00000 == PcdFdSize)'
    f = fdf.read_bytes(); assert f.count(FDF_OLD) == 1
    fdf.write_bytes(f.replace(FDF_OLD, FDF_NEW, 1))
    assert fdf.read_bytes() == (F216 / 'MacMini2020.fdf.candidate').read_bytes()
    inc = TREE / PKG / 'Include/Library'
    (inc / 'NwoasCpuPstate.h').write_bytes((F216 / 'Include/Library/NwoasCpuPstate.h').read_bytes())
    subprocess.run(['git', 'apply', str(S / 'memory-s161/nwoas-s161-guest-ram-normalize.diff')], cwd=TREE, check=True)
    (inc / 'NwoasGuestRam.h').write_bytes((S / 'memory-s161/Include/Library/NwoasGuestRam.h').read_bytes())
    (inc / 'NwoasSplitMemoryMap.h').write_bytes((F216 / 'Include/Library/NwoasSplitMemoryMap.h').read_bytes())
    hide = TREE / PKG / 'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c'
    hide.write_text('#define NWOAS_HIDE_KEEP_HIGH_BYTES 0x280000000ULL\n' + (F216 / 'NwoasHideHighRamDxe-s167.c').read_text())
    hinf = hide.with_suffix('.inf')
    hinf.write_text(hinf.read_text().replace('  BaseLib\n', '  BaseLib\n  BaseMemoryLib\n  IoLib\n  TimerLib\n'))
    # (3) patch_sources.patch: handoff header + AdtParser/MemoryInitPeiLib edits (patch files go to OUT)
    sys.path.insert(0, str(F216))
    from patch_sources import patch  # noqa: E402
    hsrc = OUT / 'patch_sources-out'; hsrc.mkdir(exist_ok=True)
    (hsrc / 'NwoasHandoffRanges.h').write_bytes((F216 / 'NwoasHandoffRanges.h').read_bytes())
    patch(TREE, hsrc)
    for rel, h in m216['source_sha256'].items():
        b = (TREE / rel).read_bytes()
        assert sha(b) == h and b == (F216 / 'source-candidate' / rel).read_bytes(), ('S216 candidate mismatch', rel)
    # (4) s131_builder.py edits: verify exact .before then write the recorded .candidate
    s131 = {
        PKG + 'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c': 'NwoasHideHighRamDxe.c',
        PKG + 'Library/DeviceBootManagerLib/DeviceBootManagerLib.c': 'DeviceBootManagerLib.c',
        PKG + 'Library/DeviceBootManagerLib/DeviceBootManagerLib.inf': 'DeviceBootManagerLib.inf',
        'MU_BASECORE/MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceIo.c': 'NonDiscoverablePciDeviceIo.c',
        PKG + 'Library/MsBootOptionsLib/MsBootOptionsLib.c': 'MsBootOptionsLib.c',
        'Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc': 'MADT_Static.aslc',
    }
    for rel, name in s131.items():
        p = TREE / rel
        assert p.read_bytes() == (F216 / 's131-build' / (name + '.before')).read_bytes(), ('s131 before mismatch', rel)
        p.write_bytes((F216 / 's131-build' / (name + '.candidate')).read_bytes())

    # ---------------------------------------------------------------- S237 change
    changed = {}
    if a.variant == 's237':
        for rel, src in ((MEMC, HERE / 'src/MemoryInitPeiLib.c'), (MEMINF, HERE / 'src/MemoryInitPeiLib.inf')):
            p = TREE / rel
            before = p.read_bytes()
            after = src.read_bytes()
            changed[rel] = {'before_sha256': sha(before), 'after_sha256': sha(after)}
            p.write_bytes(after)
        # The S237 sources must be the S216 candidate plus exactly the S237 hunk(s).
        assert changed[MEMC]['before_sha256'] == m216['source_sha256'][MEMC]
        assert changed[MEMINF]['before_sha256'] == m216['source_sha256'][MEMINF]

else:
    m216 = json.loads((F216 / 'manifest.json').read_text())
    for rel, h in m216['source_sha256'].items():
        if a.variant == 's237' and rel in (MEMC, MEMINF):
            assert (TREE / rel).read_bytes() == (HERE / 'src' / Path(rel).name).read_bytes(), rel
        elif not rel.endswith('NwoasHideHighRamDxe.c'):  # that one is checked as an s131 file below
            assert sha((TREE / rel).read_bytes()) == h, rel
    for name in ('NwoasHideHighRamDxe.c', 'DeviceBootManagerLib.c', 'DeviceBootManagerLib.inf',
                 'NonDiscoverablePciDeviceIo.c', 'MsBootOptionsLib.c', 'MADT_Static.aslc'):
        hits = [q for q in TREE.glob('**/' + name) if 'Build' not in q.parts and q.read_bytes() == (F216 / 's131-build' / (name + '.candidate')).read_bytes()]
        assert len(hits) == 1, name
    changed = {}

# ---------------------------------------------------------------- build
work = TREE
if a.logical_root:
    work = Path(a.logical_root) / 'apple_silicon_platforms_mu'
    assert work.resolve() == TREE, (work, work.resolve(), TREE)
env = os.environ.copy()
for k in ('PYTHONPATH', 'WORKSPACE', 'PACKAGES_PATH', 'EDK_TOOLS_PATH', 'CONF_PATH'):
    env.pop(k, None)
env.update(CLANG_BIN=str(TOOLBIN) + '/', CLANG_HOST_BIN='/usr/bin/', RUSTUP_TOOLCHAIN='1.88.0',
           NWOAS_GUEST_VARIANT='compat', PWD=str(work))
env['PATH'] = str(VENV / 'bin') + ':' + str(TOOLBIN) + ':' + env['PATH']
log = OUT / ('build-' + a.variant + '.log')
with log.open('w') as lf:
    r = subprocess.run(['nice', '-n', '19', str(VENV / 'bin/stuart_build'), '-c',
                        str(work / 'Platform/MacMini2020Pkg/PlatformBuild.py'),
                        'TARGET=DEBUG', 'TOOL_CHAIN_TAG=CLANGPDB', 'MAX_CONCURRENT_THREAD_NUMBER=' + a.jobs],
                       cwd=str(work), env=env, stdout=lf, stderr=subprocess.STDOUT)
if r.returncode:
    raise SystemExit('build failed %d; see %s' % (r.returncode, log))
fd = (TREE / 'Build/MacMini2020-AARCH64/DEBUG_CLANGPDB/FV/J274MACMINI2020_EFI.fd').read_bytes()

# ---------------------------------------------------------------- assembly (S216 + S223)
prefix = (S / 'loader-s215/out/m1n1.bin').read_bytes()
dtb = (S / 'dtb-s221/out/apple-j274-padded.dtb').read_bytes()
assert len(prefix) == 0x20c000 and sha(prefix) == 'd91700ccbe277461513ca9d8320263bf41fc33da8cb814ba095d0f857d02e334'
assert len(dtb) == 0x10000 and sha(dtb) == 'ecc93b24740d80007d31986a43983c36eef47dd8b0b105b663c25a7b51190f76'
assert len(fd) == 0x1d88000 and int.from_bytes(fd[16:24], 'little') == 0x1e00000, hex(len(fd))
s216_like = prefix + dtb + fd                       # firmware-s216 s131_builder assembly
pad = 0x1e00000 - len(fd); assert pad == 0x78000    # loader-s223 build_candidate tail
payload = s216_like + b'\xff' * pad
assert len(payload) - 0x21c000 == 0x1e00000 and len(payload) % 0x4000 == 0
name = {'repro': 'm1n1-payload-s237-repro-s223.bin', 's237': 'm1n1-payload-s237-bridgeresv.bin'}[a.variant]
(OUT / ('fd-' + a.variant + '.fd')).write_bytes(fd)
(OUT / name).write_bytes(payload)
res = {
    'variant': a.variant, 'tree': str(TREE), 'workspace_path_used': str(work),
    'fd_sha256': sha(fd), 'fd_bytes': len(fd),
    's216_layout_payload_sha256': sha(s216_like),
    'payload': str(OUT / name), 'payload_sha256': sha(payload), 'payload_bytes': len(payload),
    'reference': {'s216_fd_sha256': 'b008dcfc93059ee6f623dc7f8d96dd69276970c14931be68f8e7dce256e537cc',
                  's216_payload_sha256': '0ae1cb75bfc3cbc2b7b15e6408c489fe5badd3d12d53fc4353a85b6b5ad3d197',
                  's223_payload_sha256': '2343f7fc35acd0401bafd63bfdc10d047ad8c4bc78207056c42e478051a8b5e7'},
    'changed_sources': changed,
}
res['fd_identical_to_s216'] = res['fd_sha256'] == res['reference']['s216_fd_sha256']
res['payload_identical_to_s223'] = res['payload_sha256'] == res['reference']['s223_payload_sha256']
(OUT / ('result-' + a.variant + '.json')).write_text(json.dumps(res, indent=2) + '\n')
print(json.dumps(res, indent=2))
