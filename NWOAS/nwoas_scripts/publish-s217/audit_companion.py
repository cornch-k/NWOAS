#!/usr/bin/env python3
"""S217 metadata audit for the publish-s217 companion export.

Read-only. Never touches the live checkouts: base blobs are read with `git show`,
patches are applied with `git apply` inside a temporary directory that is not a
git repository. Prints PASS/FAIL lines and a candidate-file exclusion report.
No private values are printed (only file names, counts and hashes we already
publish in manifests).
"""
from pathlib import Path
import hashlib, json, re, subprocess, sys, tempfile, collections

ROOT = Path('/Volumes/X31/NWOAS')
SCR = ROOT / 'nwoas_scripts'
PUB = SCR / 'publish-s217'
COMP = PUB / 'companion'
REPOS = {
    'm1n1_windows': ROOT / 'm1n1_windows-s159',
    'apple_silicon_platforms_mu': ROOT / 'apple_silicon_platforms_mu',
    'MU_BASECORE': ROOT / 'apple_silicon_platforms_mu/MU_BASECORE',
    'ARM_TIANO': ROOT / 'apple_silicon_platforms_mu/Silicon/ARM/TIANO',
    'm1n1_guest': ROOT / 'm1n1-guest-s215',
}
PKG = 'Silicon/Apple/AppleSiliconPkg/'
fails = 0


def ok(cond, msg):
    global fails
    print(('PASS ' if cond else 'FAIL ') + msg)
    if not cond:
        fails += 1


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def git(repo, *args, data=None):
    return subprocess.run(['git', '-C', str(repo), *args], input=data,
                          capture_output=True, check=False)


def apply_component(entry):
    """Return {relpath: bytes} after applying the patch over base blobs in a temp dir."""
    repo = REPOS[entry['component']]
    base = entry['base_commit']
    patch = (COMP / entry['patch']).read_bytes()
    ok(sha(patch) == entry['sha256'], f"{entry['component']}: patch sha256 matches manifest")
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        for rel in entry['files']:
            r = git(repo, 'show', f'{base}:{rel}')
            if r.returncode == 0:  # existing file at base; new files simply absent
                p = td / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(r.stdout)
        r = subprocess.run(['git', 'apply', '--whitespace=nowarn', '-'], cwd=td,
                           input=patch, capture_output=True)
        ok(r.returncode == 0, f"{entry['component']}: patch applies over base blobs outside any repo")
        return {rel: (td / rel).read_bytes() for rel in entry['files'] if (td / rel).exists()}


def main():
    m = json.loads((COMP / 'manifest.json').read_text())
    comps = {e['component']: e for e in m['components']}

    # 1. Base commits: exist locally and are reachable from a remote-tracking branch (no network).
    for name, e in comps.items():
        repo = REPOS[name]
        exists = git(repo, 'cat-file', '-e', e['base_commit'] + '^{commit}').returncode == 0
        ok(exists, f"{name}: base {e['base_commit'][:10]} exists locally")
        r = git(repo, 'branch', '-r', '--contains', e['base_commit'])
        branches = [b.strip() for b in r.stdout.decode().splitlines()]
        ok(bool(branches), f"{name}: base reachable from remote-tracking branch(es) {branches}")

    # 2. Apply patches and compare with recorded build inputs.
    out = {n: apply_component(e) for n, e in comps.items()}

    fw = json.loads((SCR / 'firmware-s216/manifest.json').read_text())
    uefi = out['apple_silicon_platforms_mu']
    for rel, h in fw['source_sha256'].items():
        if rel.endswith('DSDT.asl'):
            # DSDT unchanged in S216 (not in patch); verify against base blob instead.
            b = git(REPOS['apple_silicon_platforms_mu'], 'show', f"{comps['apple_silicon_platforms_mu']['base_commit']}:{rel}").stdout
            ok(sha(b) == h, f"S216 outer: {Path(rel).name} unchanged at base == firmware-s216 manifest")
            continue
        if rel.endswith('NwoasHideHighRamDxe.c'):
            # firmware-s216/manifest.json records the pre-S131 file (NWOAS_UEFI_NVME 0); the build
            # input is the s131-build candidate (NVME 1). The export correctly ships the latter.
            src_cand = (SCR / 'firmware-s216/source-candidate' / rel).read_bytes()
            ok(sha(src_cand) == h, 'S216 outer: NwoasHideHighRamDxe.c source-candidate == firmware-s216 manifest (pre-S131 flip)')
            continue
        ok(rel in uefi and sha(uefi[rel]) == h, f"S216 outer: {Path(rel).name} after patch == firmware-s216 manifest")
    for name, h in fw['rtc_source_sha256'].items():
        rel = PKG + 'Library/NwoasHardwareBootRtcLib/' + name
        ok(rel in uefi and sha(uefi[rel]) == h, f"S216 RTC lib: {name} after patch == firmware-s216 manifest")

    headers = {
        PKG + 'Include/Library/NwoasCpuPstate.h': SCR / 'firmware-s216/Include/Library/NwoasCpuPstate.h',
        PKG + 'Include/Library/NwoasSplitMemoryMap.h': SCR / 'firmware-s216/Include/Library/NwoasSplitMemoryMap.h',
        PKG + 'Include/Library/NwoasGuestRam.h': SCR / 'memory-s161/Include/Library/NwoasGuestRam.h',
    }
    for rel, src in headers.items():
        ok(uefi.get(rel) == src.read_bytes(), f'S216 header: {Path(rel).name} after patch == {src.relative_to(SCR)}')

    s131 = SCR / 'firmware-s216/s131-build'
    inner = {
        PKG + 'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c': 'NwoasHideHighRamDxe.c.candidate',
        PKG + 'Library/DeviceBootManagerLib/DeviceBootManagerLib.c': 'DeviceBootManagerLib.c.candidate',
        PKG + 'Library/DeviceBootManagerLib/DeviceBootManagerLib.inf': 'DeviceBootManagerLib.inf.candidate',
        PKG + 'Library/MsBootOptionsLib/MsBootOptionsLib.c': 'MsBootOptionsLib.c.candidate',
        'Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc': 'MADT_Static.aslc.candidate',
    }
    for rel, cand in inner.items():
        ok(rel in uefi and uefi[rel] == (s131 / cand).read_bytes(), f"S131 inner override: {cand} == patched tree")
    pci = 'MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceIo.c'
    ok(out['MU_BASECORE'].get(pci) == (s131 / 'NonDiscoverablePciDeviceIo.c.candidate').read_bytes(),
       'S131 inner override: NonDiscoverablePciDeviceIo.c.candidate == patched MU_BASECORE')
    hide = uefi.get(PKG + 'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c', b'')
    ok(b'#define NWOAS_UEFI_NVME 1 ' in hide, 'Hide driver: NWOAS_UEFI_NVME 1 (S131 flip) present')
    ok(hide.startswith(b'#define NWOAS_HIDE_KEEP_HIGH_BYTES 0x280000000ULL\n'), 'Hide driver: 10 GiB keep prefix present (S180/S216)')
    madt = uefi.get('Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc', b'')
    ok(madt.count(b'EFI_ACPI_6_3_GIC_ENABLED') >= 8 and b'AP disabled for uniprocessor boot' not in madt,
       'MADT: all eight GICC entries enabled')
    dsc = uefi.get(PKG + 'AppleSiliconPkg.dsc.inc', b'')
    ok(b'RealTimeClockLib|AppleSiliconPkg/Library/NwoasHardwareBootRtcLib/NwoasHardwareBootRtcLib.inf' in dsc
       and b'AppleSiliconPkg/Library/VirtualRealTimeClockLib/VirtualRealTimeClockLib.inf' not in dsc,
       'DSC: AppleSiliconPkg RealTimeClockLib mapping swapped to NwoasHardwareBootRtcLib (generic EmbeddedPkg entry may remain earlier)')

    # 3. S163 gap50 HV: exported hv_exc.c/hv_vm.c equal the recorded S163 sources; flags documented.
    m1 = out['m1n1_windows']
    ok(m1.get('src/hv_exc.c') == (SCR / 'nvme-s208/hv_exc-s208.c').read_bytes(), 'HV: patched hv_exc.c == nvme-s208/hv_exc-s208.c')
    ok(m1.get('src/hv_vm.c') == (SCR / 'nvme-s208/hv_vm-s208.c').read_bytes(), 'HV: patched hv_vm.c == nvme-s208/hv_vm-s208.c')
    ok(b'#define NWOAS_NVME_REASSERT_US 0' in m1.get('src/hv_exc.c', b''), 'HV: source default REASSERT_US=0 (50us only via build flag)')
    ok('-DNWOAS_NVME_REASSERT_US=50' in m['hv_build_flags'] and '-DNWOAS_NVME_MAX_BLOCKS=256' in m['hv_build_flags'],
       'HV: manifest hv_build_flags carry REASSERT_US=50 and MAX_BLOCKS=256')
    g50 = json.loads((SCR / 'nvme-s163/manifest-gap50.json').read_text())
    ok(g50['gap_us'] == 50 and len(g50['sha256']) == 64, 'HV: nvme-s163/manifest-gap50.json pins the gap50 binary hash')
    ok(m1.get('proxyclient/tools/run_guest.py') == (ROOT / 'm1n1_windows/proxyclient/tools/run_guest.py').read_bytes(),
       'HV host: run_guest.py exported from live m1n1_windows (--strict-init)')

    ok(uefi.get(PKG+'Include/Library/NwoasHandoffRanges.h') == (SCR/'firmware-s216/NwoasHandoffRanges.h').read_bytes(), 'S216 handoff range header matches')
    ok(uefi.get('Platform/MacMini2020Pkg/MacMini2020.fdf') == (SCR/'firmware-s216/MacMini2020.fdf.candidate').read_bytes(), 'S216 image header source matches')
    guest=out['m1n1_guest']
    ok(guest.get('src/payload.c') == (SCR/'loader-s215/payload-s215.c').read_bytes(), 'S215 guest payload source matches built snapshot')
    ok(guest.get('src/nwoas_handoff_layout.h') == (SCR/'loader-s215/handoff_layout.h').read_bytes(), 'S215 range policy matches built header')
    script='proxyclient/tools/run_guest.py'
    mode=git(REPOS['m1n1_windows'],'ls-tree',comps['m1n1_windows']['base_commit'],'--',script).stdout.decode().split()[0]
    patch=(COMP/comps['m1n1_windows']['patch']).read_text()
    part=patch.split('diff --git a/'+script+' b/'+script+'\n')[1].split('diff --git ')[0]
    ok(mode=='100755' and 'new mode 100644' not in part, 'run_guest.py executable mode preserved')

    print(f'\nRESULT: {fails} failure(s)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
