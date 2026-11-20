#!/usr/bin/env python3
"""S280 (TEST PAYLOAD): the S261 payload (daily S268 payload, BT SSDT) with ONE more change, the S260 MADT edit:
GICC EfficiencyClass = 1 for the four Firestorm P-cores (GICC 4..7, MPIDR 0x10100..0x10103; Icestorm 0..3 stay 0),
byte-for-byte the madt_edit() of fw-power-s260/build.py (4 source bytes).  Copy of fw-bt-s261/build.py; every S261
edit is executed unchanged first.  Only the 'btmadt' variant exists.  Payload m1n1-payload-s280-btmadt.bin.

Original S261 docstring:
S261 (TEST PAYLOAD): the S259 payload (S256 + Wi-Fi NWC0/WLN0) plus ONE more SSDT, J274BTH:
\\_SB.NWC1 (ACPI0004) with BTH0 _HID "NWOA5F69", the built-in Bluetooth of nwoas_scripts/bt-s261
(contract research-20261118/bt/CONTRACT-BT.md).  Copy of fw-wifi-s259/build.py; every S259 edit is executed
unchanged (same IORT edit, same MemoryInitPeiLib.c with the 16 MiB 0xE0000000 reservation, which already
covers the wider S261 root port window), the Wi-Fi SSDT must come out byte-identical to S259's AML.
No IORT node is added for BTH0: the Bluetooth driver allocates contiguous memory itself and creates no DMA
adapter.  Only the 'bt' variant exists.

Original S259 docstring:
S259 (TEST PAYLOAD): the S256 payload (S247 + SPK0 audio SSDT) plus the built-in Wi-Fi of
nwoas_scripts/wifi-s259 (contract research-20261118/wifi/CONTRACT-WIN.md). Copy of fw-audio-s256/build.py.
Changes on top of S256 (everything else is executed unchanged, S256 inputs are read-only and pinned):
  1. SSDT J274WIFI: \\_SB.NWC0 (ACPI0004, _CRS producer, _DMA, _CCA 0) with WLN0 _HID "NWOA4378"
     (nwoas_scripts/wifi-s259/acpi), resource values checked against nwoas_wifi_s259.py before editing.
  2. IORT: a third node, Named Component "\\_SB_.NWC0.WLN0", same field values as the XHC0 node.
  3. MemoryInitPeiLib.c: S247 file + ReserveMemoryRegion(0xE0000000, 16 MiB) after the S237 one
     (fw-wifi-s259/src/MemoryInitPeiLib.c, pinned). MemoryInitPeiLib.inf = S247 file, unchanged.
Only the 'wifi' variant exists.

Original S256 docstring:
S256 (TEST PAYLOAD): the S247 payload (S237 + J274 AGX0 SSDT + AGX local reserve) plus ONE
more SSDT: SPK0 / _HID "NWAU0001", the J274 internal speaker device of research-20261116/sound/
PLAN-S256.md. Copy of fw-agx-s247/build.py; the S247 edits are executed unchanged (S247 src/ and
port-j274/gpu/acpi-j274 are only read and their hashes are pinned), then the audio SSDT is added.
The SSDT window address must equal S256_WIN_IPA in nwoas_scripts/sound-s256/audio_module.py
(checked below before anything is edited). Only the 'audio' variant exists.

Original S247 docstring:
S247 (TEST PAYLOAD): S237 guest payload + J274 AGX0 ACPI SSDT + AGX local-reserve
handling in MemoryPeim, for the m1n1-gpu-hv2 MEDIATE stage. Copy of fw-s237/build.py;
only the 'agx' variant exists. fw-s237/, firmware-s216/ and the source of record are only
read. Built at the native path (no /Volumes/X31 logical root), so it is NOT byte-identical
to S237 in embedded paths; reproducibility is proven by rebuilding in the same tree.

  --variant agx : S216 source state + S237 bridge window + S247 AGX changes

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
import argparse, difflib, hashlib, json, os, re, subprocess, sys
sys.dont_write_bytecode = True  # never write __pycache__ into firmware-s216

ROOT = Path('/Users/angigyeom/Desktop/DEV/NWOAS')
S = ROOT / 'nwoas_scripts'
F216 = S / 'firmware-s216'
HERE = S / 'fw-btmadt-s280'
S259DIR = S / 'fw-wifi-s259'
BTDIR = S / 'bt-s261'
AUDDIR = S / 'fw-audio-s256' / 'acpi'
WIFI = S / 'wifi-s259'
S247 = S / 'fw-agx-s247'
AUDMOD = S / 'sound-s256/audio_module.py'
S237 = S / 'fw-s237'
ACPI = ROOT / 'port-j274/gpu/acpi-j274'
TOOLBIN = S237 / 'toolbin'
VENV = ROOT / 'fw-s237-mu/venv'
PKG = 'Silicon/Apple/AppleSiliconPkg/'
MEMC = 'Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c'
MEMINF = 'Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.inf'
ACPIDIR = 'Platform/MacMini2020Pkg/AcpiTables/'
ACPIINF = ACPIDIR + 'DeviceAcpiTables.inf'
DSC = 'Platform/MacMini2020Pkg/MacMini2020.dsc'
ASL = ('J274AppleAgxAbiAdmission.asl.inc', 'J274AppleAgxAbiAdmissionSsdt.asl')
AUD_ASL = ('NwoasAudioJ274.asl.inc', 'NwoasAudioJ274Ssdt.asl')
INF_OLD = b'[Sources]\n'
INF_ADD = (b'  J274AppleAgxAbiAdmissionSsdt.asl   # NWOAS S247 (test payload): AGX0 for the windows-on-m1 KMD\n'
           b'  NwoasAudioJ274Ssdt.asl   # NWOAS S256 (test payload): SPK0 NWAU0001 internal speaker\n'
           b'  NwoasWifiJ274Ssdt.asl   # NWOAS S259 (test payload): NWC0/WLN0 NWOA4378 built-in Wi-Fi\n'
           b'  NwoasBtJ274Ssdt.asl   # NWOAS S261 (test payload): NWC1/BTH0 NWOA5F69 built-in Bluetooth\n')
WIFI_ASL = ('NwoasWifiJ274.asl.inc', 'NwoasWifiJ274Ssdt.asl')
BT_ASL = ('NwoasBtJ274.asl.inc', 'NwoasBtJ274Ssdt.asl')
S259_WIFI_AML_SHA = 'bcdcbb03b167eac4a845a8efcb4ba05a0a6f9caa46732b76fd63364221643254'
IORT = ACPIDIR + 'IORT.aslc'
IORT_SHA = 'e21f55237244356a4b762254239cf82381dfc4adebf15074f512b8e05ca7ea64'   # source of record
MEMC_S259 = S259DIR / 'src/MemoryInitPeiLib.c'
MEMC_S259_SHA = '02e220b4f69d01fc22b656b88bd9e8883f51557c49c957abda9ea49fc2d4741c'
# S256 inputs re-used read-only (hashes of the files that produced the S256 payload 3cb9e2df...)
S256_PIN = {AUDDIR / 'NwoasAudioJ274.asl.inc': 'bb55894116caadde800a675b3c7d4645d50503b7c67134d902c21806d30eff02',
            AUDDIR / 'NwoasAudioJ274Ssdt.asl': '830227167dea1deb66821f884d2abce5261506dc734b017d81494cc2917684c9'}
# audio_module.py is not pinned here (it changes with the sound work); only its window constants
# matter for the firmware and they are checked against the SSDT above, as in fw-audio-s256/build.py.
IORT_EDITS = [
    (b'#define NWOAS_XHC0_OBJECT_NAME  "\\\\_SB_.SCB0.XHC0"\n',
     b'#define NWOAS_XHC0_OBJECT_NAME  "\\\\_SB_.SCB0.XHC0"\n'
     b'// NWOAS S259 (test payload): Wi-Fi platform device, same length as the XHC0 name (15 + NUL).\n'
     b'#define NWOAS_WLN0_OBJECT_NAME  "\\\\_SB_.NWC0.WLN0"\n'),
    (b'  NWOAS_IORT_RC Nvme;\n} NWOAS_IORT_TABLE;',
     b'  NWOAS_IORT_RC Nvme;\n  NWOAS_IORT_NAMED_COMP_NODE       Wlan;   // NWOAS S259\n} NWOAS_IORT_TABLE;'),
    (b'    2,                                          // NumNodes',
     b'    3,                                          // NumNodes (NWOAS S259: + Wlan)'),
    (b'  { { EFI_ACPI_IORT_TYPE_ROOT_COMPLEX, sizeof (NWOAS_IORT_RC), 0, 1, 0, 0 },\n    1, 0, 0, 0, 0, 1 }\n};',
     b'  { { EFI_ACPI_IORT_TYPE_ROOT_COMPLEX, sizeof (NWOAS_IORT_RC), 0, 1, 0, 0 },\n    1, 0, 0, 0, 0, 1 },\n'
     b'  // NWOAS S259: Wi-Fi Named Component, field values identical to the XHC0 node above\n'
     b'  {\n    {\n      {\n'
     b'        EFI_ACPI_IORT_TYPE_NAMED_COMP,          // Type (1 = Named Component)\n'
     b'        sizeof (NWOAS_IORT_NAMED_COMP_NODE),    // Length (node + ObjectName)\n'
     b'        0,                                      // Revision\n'
     b'        2,                                      // Identifier\n'
     b'        0,                                      // NumIdMappings  -- ZERO: no ITS/SMMU ref (NO MSI)\n'
     b'        0                                       // IdReference\n'
     b'      },\n'
     b'      0,                                        // Flags\n'
     b'      0,                                        // CacheCoherent (0, matches _CCA=0)\n'
     b'      0,                                        // AllocationHints\n'
     b'      0,                                        // Reserved\n'
     b'      0,                                        // MemoryAccessFlags\n'
     b'      40                                        // AddressSizeLimit (bits)\n'
     b'    },\n'
     b'    NWOAS_WLN0_OBJECT_NAME                       // "\\\\_SB_.NWC0.WLN0\\0"\n'
     b'  }\n};'),
]
# S247 inputs that this build re-uses read-only (hashes = fw-agx-s247/out/s247/result-agx.json)
S247_PIN = {S247 / 'src/MemoryInitPeiLib.c': 'd90b5685a58f493b157c0c18497d7e6762d9e59bdc61df28fbc9504d71b1edce',
            S247 / 'src/MemoryInitPeiLib.inf': '8115fe09b094e81460a483d7887c64e7a16a9c6e026326b0755732a8ce9de476',
            ACPI / ASL[0]: '574e7dc97f3d22fec1348bc2d9f84bfcf5a61955979b55ebf8a7d6b10b18db04',
            ACPI / ASL[1]: 'a3ab7f406ca437f0380a1fe91fcac0e967b3bdaee6bbcd53e167b9b6350dcc5f'}
DSC_OLD = b'[BuildOptions.common]\n'
DSC_ADD = b'  *_*_*_ASLPP_FLAGS = -DAGX_LOCAL_RESERVE_V2   # NWOAS S247: local-reserve ABI v3 in the AGX0 SSDT\n'


def sha(b):
    return hashlib.sha256(b).hexdigest()


ap = argparse.ArgumentParser()
ap.add_argument('--tree', required=True)
ap.add_argument('--variant', choices=['btmadt'], required=True)
ap.add_argument('--logical-root', default=None,
                help='symlinked path whose apple_silicon_platforms_mu entry resolves to --tree')
ap.add_argument('--out', required=True)
ap.add_argument('--jobs', default='2')
ap.add_argument('--rebuild-only', action='store_true',
                help='repeat the build in a tree this script already edited (its Build dir moved away first)')
a = ap.parse_args()
MADT = 'Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc'


def madt_edit(b):
    """S260: EfficiencyClass (11th macro argument) 0 -> 1 for the four GET_MPIDR(0, 1, 1, n) entries."""
    out = b
    for n in range(4):
        pat = re.compile(rb'(GET_MPIDR\(0, 1, 1, %d\),\s+EFI_ACPI_6_3_GIC_ENABLED, \d+, 0,\s+0, 0, 25, 0 /\* GicRBase \*/, )0(, 0\),)' % n)
        out, k = pat.subn(rb'\g<1>1\2', out)
        assert k == 1, ('MADT P-core entry not found exactly once', n, k)
    assert out.count(b'/* GicRBase */, 1, 0),') == 4 and out.count(b'/* GicRBase */, 0, 0),') == 4
    assert len(out) == len(b) and sum(x != y for x, y in zip(out, b)) == 4
    return out
TREE = Path(a.tree).resolve()
OUT = Path(a.out); OUT.mkdir(parents=True, exist_ok=True)
assert TREE != (ROOT / 'apple_silicon_platforms_mu').resolve(), 'refuse to edit the source of record'
assert not (TREE / 'Build').exists(), 'tree already built; use a fresh clone'
for q, h in S247_PIN.items():
    assert sha(q.read_bytes()) == h, ('S247 input changed', q)
# window constant: audio_module.py <-> SSDT resource 4
win = int(re.search(rb'^S256_WIN_IPA = (0x[0-9a-fA-F]+)$', AUDMOD.read_bytes(), re.M).group(1), 16)
wsz = int(re.search(rb'^S256_WIN_SIZE = (0x[0-9a-fA-F]+)$', AUDMOD.read_bytes(), re.M).group(1), 16)
inc = (AUDDIR / AUD_ASL[0]).read_text()
assert ('0x%016X, 0x%016X, 0x0,' % (win, win + wsz - 1)) in inc and ('0x%016X, ,, ,' % wsz) in inc, \
    ('SSDT window resource != audio_module.py S256_WIN_IPA/S256_WIN_SIZE', hex(win), hex(wsz))
assert inc.count('QWordMemory') == 5 and '"NWAU0001"' in inc and 'Interrupt' not in inc
for q, h in S256_PIN.items():
    assert sha(q.read_bytes()) == h, ('S256 input changed', q)
assert sha(MEMC_S259.read_bytes()) == MEMC_S259_SHA, 'fw-wifi-s259/src/MemoryInitPeiLib.c changed'
# wifi constants: nwoas_wifi_s259.py <-> SSDT (same rule as the audio window check)
sys.path.insert(0, str(WIFI))
import nwoas_wifi_s259 as W  # noqa: E402
winc = (WIFI / 'acpi' / WIFI_ASL[0]).read_text()
for base, size in ((W.BAR0_BUS + W.CPU_OFFSET, W.BAR0_SIZE), (W.BAR2_BUS + W.CPU_OFFSET, W.BAR2_SIZE),
                   (W.CTRL_IPA, W.CTRL_SIZE), (W.WINDOW_BUS[0] + W.CPU_OFFSET, W.WINDOW_BUS[1] - W.WINDOW_BUS[0] + 1)):
    assert ('0x%016X, 0x%016X, 0x0,' % (base, base + size - 1)) in winc, ('SSDT != nwoas_wifi_s259.py', hex(base))
assert '"NWOA4378"' in winc and ('{ %d }' % W.WLAN_IRQ) in winc and ('0x%08X' % W.MAGIC) in winc
assert W.RESERVED_RAM == (0xE0000000, 0x1000000) and b'WifiWindowBase = 0xE0000000ULL' in MEMC_S259.read_bytes()
assert W.RESERVED_RAM[0] <= W.WINDOW_BUS[0] and W.WINDOW_BUS[1] < sum(W.RESERVED_RAM)
# bt constants: nwoas_bt_s261.py <-> BT SSDT; the wider S261 window must stay inside the same reservation
sys.path.insert(0, str(BTDIR))
import nwoas_bt_s261 as B  # noqa: E402
binc = (BTDIR / 'acpi' / BT_ASL[0]).read_text()
for base, size in ((B.FN1_BAR0_BUS + B.CPU_OFFSET, B.FN1_BAR0_SIZE), (B.FN1_BAR2_BUS + B.CPU_OFFSET, B.FN1_BAR2_SIZE),
                   (B.BT_CTRL_IPA, B.BT_CTRL_SIZE),
                   (B.BT_RESOURCE_WINDOW[0], B.BT_RESOURCE_WINDOW[1] - B.BT_RESOURCE_WINDOW[0] + 1)):
    assert ('0x%016X, 0x%016X, 0x0,' % (base, base + size - 1)) in binc, ('BT SSDT != nwoas_bt_s261.py', hex(base))
assert '"NWOA5F69"' in binc and ('0x%08X' % B.BT_MAGIC) in binc and 'NWC1' in binc
assert W.RESERVED_RAM[0] <= B.WINDOW_S261[0] and B.WINDOW_S261[1] < sum(W.RESERVED_RAM)
assert B.BT_RESOURCE_WINDOW[0] > W.WINDOW_BUS[1] + W.CPU_OFFSET, 'NWC1 producer overlaps NWC0'

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
    # S280 = S260: the MADT EfficiencyClass edit (on top of the S216/s131 MADT candidate)
    p = TREE / MADT
    b = p.read_bytes(); assert b == (F216 / 's131-build' / 'MADT_Static.aslc.candidate').read_bytes()
    n = madt_edit(b)
    p.write_bytes(n)
    madt_change = {'before_sha256': sha(b), 'after_sha256': sha(n)}

    # ---------------------------------------------------------------- S237 + S247 change
    changed = {MADT: madt_change}
    for rel, src in ((MEMC, MEMC_S259), (MEMINF, S247 / 'src/MemoryInitPeiLib.inf')):
        p = TREE / rel
        before = p.read_bytes()
        assert sha(before) == m216['source_sha256'][rel], ('not the S216 candidate', rel)
        after = src.read_bytes()
        changed[rel] = {'before_sha256': sha(before), 'after_sha256': sha(after)}
        p.write_bytes(after)
    for name in ASL:
        p = TREE / ACPIDIR / name
        assert not p.exists(), p
        b = (ACPI / name).read_bytes()
        p.write_bytes(b)
        changed[ACPIDIR + name] = {'before_sha256': None, 'after_sha256': sha(b)}
    for name in AUD_ASL:
        p = TREE / ACPIDIR / name
        assert not p.exists(), p
        b = (AUDDIR / name).read_bytes()
        p.write_bytes(b)
        changed[ACPIDIR + name] = {'before_sha256': None, 'after_sha256': sha(b)}
    for name in WIFI_ASL:
        p = TREE / ACPIDIR / name
        assert not p.exists(), p
        b = (WIFI / 'acpi' / name).read_bytes()
        p.write_bytes(b)
        changed[ACPIDIR + name] = {'before_sha256': None, 'after_sha256': sha(b)}
    for name in BT_ASL:
        p = TREE / ACPIDIR / name
        assert not p.exists(), p
        b = (BTDIR / 'acpi' / name).read_bytes()
        p.write_bytes(b)
        changed[ACPIDIR + name] = {'before_sha256': None, 'after_sha256': sha(b)}
    p = TREE / IORT
    b = p.read_bytes(); assert sha(b) == IORT_SHA, ('IORT.aslc not the source of record', sha(b))
    n = b
    for old, new in IORT_EDITS:
        assert n.count(old) == 1, ('IORT edit anchor', old[:60])
        n = n.replace(old, new, 1)
    changed[IORT] = {'before_sha256': sha(b), 'after_sha256': sha(n)}
    p.write_bytes(n)
    for rel, old, add in ((ACPIINF, INF_OLD, INF_ADD), (DSC, DSC_OLD, DSC_ADD)):
        p = TREE / rel
        b = p.read_bytes(); assert b.count(old) == 1 and add not in b, rel
        n = b.replace(old, old + add, 1)
        changed[rel] = {'before_sha256': sha(b), 'after_sha256': sha(n)}
        p.write_bytes(n)

else:
    m216 = json.loads((F216 / 'manifest.json').read_text())
    for rel, h in m216['source_sha256'].items():
        if rel in (MEMC, MEMINF):
            assert (TREE / rel).read_bytes() == (MEMC_S259 if rel == MEMC else S247 / 'src' / Path(rel).name).read_bytes(), rel
        elif not rel.endswith('NwoasHideHighRamDxe.c'):
            assert sha((TREE / rel).read_bytes()) == h, rel
    for name in ASL:
        assert (TREE / ACPIDIR / name).read_bytes() == (ACPI / name).read_bytes(), name
    for name in AUD_ASL:
        assert (TREE / ACPIDIR / name).read_bytes() == (AUDDIR / name).read_bytes(), name
    for name in WIFI_ASL:
        assert (TREE / ACPIDIR / name).read_bytes() == (WIFI / 'acpi' / name).read_bytes(), name
    for name in BT_ASL:
        assert (TREE / ACPIDIR / name).read_bytes() == (BTDIR / 'acpi' / name).read_bytes(), name
    assert b'NWOAS_WLN0_OBJECT_NAME' in (TREE / IORT).read_bytes()
    assert INF_ADD in (TREE / ACPIINF).read_bytes() and DSC_ADD in (TREE / DSC).read_bytes()
    for name in ('NwoasHideHighRamDxe.c', 'DeviceBootManagerLib.c', 'DeviceBootManagerLib.inf',
                 'NonDiscoverablePciDeviceIo.c', 'MsBootOptionsLib.c'):
        hits = [q for q in TREE.glob('**/' + name) if 'Build' not in q.parts and q.read_bytes() == (F216 / 's131-build' / (name + '.candidate')).read_bytes()]
        assert len(hits) == 1, name
    assert (TREE / MADT).read_bytes() == madt_edit((F216 / 's131-build' / 'MADT_Static.aslc.candidate').read_bytes())
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
name = 'm1n1-payload-s280-btmadt.bin'
(OUT / ('fd-' + a.variant + '.fd')).write_bytes(fd)
(OUT / name).write_bytes(payload)
res = {
    'variant': a.variant, 'tree': str(TREE), 'workspace_path_used': str(work),
    'fd_sha256': sha(fd), 'fd_bytes': len(fd),
    's216_layout_payload_sha256': sha(s216_like),
    'payload': str(OUT / name), 'payload_sha256': sha(payload), 'payload_bytes': len(payload),
    'window_ipa': hex(win), 'window_bytes': hex(wsz),
    'reference': {'s247_payload_sha256': 'fb2011f96aea4ca30c69d5bd706a379a04010dc7895366b5c86e191444ee641c',
                  's247_fd_sha256': 'ebd3bb723e9ade72e7096eeadef85889a206672ed13c449664b805c6a086bd6a',
                  's247_agx_ssdt_aml_sha256': '15347541e59d46a36cb2e6b1953b55f52e0b01d7baa6484b8795add06fcf240a',
                  's216_fd_sha256': 'b008dcfc93059ee6f623dc7f8d96dd69276970c14931be68f8e7dce256e537cc',
                  's216_payload_sha256': '0ae1cb75bfc3cbc2b7b15e6408c489fe5badd3d12d53fc4353a85b6b5ad3d197',
                  's223_payload_sha256': '2343f7fc35acd0401bafd63bfdc10d047ad8c4bc78207056c42e478051a8b5e7'},
    'changed_sources': changed,
}
res['fd_identical_to_s216'] = res['fd_sha256'] == res['reference']['s216_fd_sha256']
res['payload_identical_to_s223'] = res['payload_sha256'] == res['reference']['s223_payload_sha256']
# Content checks of the built FD (strings are stored UTF-8/ASCII in DEBUG images; the DXE FV is LZMA
# compressed, so the SSDT is checked in the Build tree's AML instead).
aml = list((TREE / 'Build').glob('**/J274AppleAgxAbiAdmissionSsdt.aml'))
res['ssdt_aml'] = [{'path': str(q), 'sha256': sha(q.read_bytes()), 'bytes': len(q.read_bytes()),
                    'has_APPL0002': b'APPL0002' in q.read_bytes(), 'has_J274ABI': b'J274ABI' in q.read_bytes()} for q in aml]
res['agx_ssdt_identical_to_s247'] = [x['sha256'] for x in res['ssdt_aml']] == [res['reference']['s247_agx_ssdt_aml_sha256']]
aud = list((TREE / 'Build').glob('**/NwoasAudioJ274Ssdt.aml'))
res['audio_ssdt_aml'] = [{'path': str(q), 'sha256': sha(q.read_bytes()), 'bytes': len(q.read_bytes()),
                          'has_NWAU0001': b'NWAU0001' in q.read_bytes(), 'has_SPK0': b'SPK0' in q.read_bytes(),
                          'has_window': (win).to_bytes(8, 'little') in q.read_bytes()} for q in aud]
prepi = list((TREE / 'Build').glob('**/MemoryInitPeiLib*/**/*.lib')) + list((TREE / 'Build').glob('**/PrePi*.efi'))
res['s247_string_in_build'] = [str(q) for q in prepi if b'S247 AGX local reserve' in q.read_bytes()]
res['audio_ssdt_identical_to_s256'] = [x['sha256'] for x in res['audio_ssdt_aml']] == \
    ['6621c8591f95f91bcb3ad8ef88e9f99d93d349870d030a4bf651b7a4f646ae9d']
wifi = list((TREE / 'Build').glob('**/NwoasWifiJ274Ssdt.aml'))
res['wifi_ssdt_aml'] = [{'path': str(q), 'sha256': sha(q.read_bytes()), 'bytes': len(q.read_bytes()),
                         'has_NWOA4378': b'NWOA4378' in q.read_bytes(), 'has_WLN0': b'WLN0' in q.read_bytes(),
                         'has_NWC0': b'NWC0' in q.read_bytes(),
                         'has_ctrl_ipa': (W.CTRL_IPA).to_bytes(8, 'little') in q.read_bytes(),
                         'has_magic': (W.MAGIC).to_bytes(4, 'little') in q.read_bytes()} for q in wifi]
iort = [q for q in (TREE / 'Build').glob('**/IORT*') if q.is_file() and q.suffix in ('.acpi', '.aml', '.bin', '.efi', '.lib', '.dll', '.obj', '.o')]
res['iort_build_files'] = [{'path': str(q), 'has_WLN0_name': b'\\_SB_.NWC0.WLN0' in q.read_bytes(),
                            'has_XHC0_name': b'\\_SB_.SCB0.XHC0' in q.read_bytes()} for q in iort]
res['s259_string_in_build'] = [str(q) for q in prepi if b'S259 reserved Wi-Fi bridge window' in q.read_bytes()]
res['wifi_ssdt_identical_to_s259'] = [x['sha256'] for x in res['wifi_ssdt_aml']] == [S259_WIFI_AML_SHA]
bt = list((TREE / 'Build').glob('**/NwoasBtJ274Ssdt.aml'))
res['bt_ssdt_aml'] = [{'path': str(q), 'sha256': sha(q.read_bytes()), 'bytes': len(q.read_bytes()),
                       'has_NWOA5F69': b'NWOA5F69' in q.read_bytes(), 'has_BTH0': b'BTH0' in q.read_bytes(),
                       'has_NWC1': b'NWC1' in q.read_bytes(),
                       'has_ctrl_ipa': (B.BT_CTRL_IPA).to_bytes(8, 'little') in q.read_bytes(),
                       'has_magic': (B.BT_MAGIC).to_bytes(4, 'little') in q.read_bytes()} for q in bt]
res['reference']['s259_payload_sha256'] = 'fbc4406b0103cf028be5be2eb8ec60844af8952d9c845405338d48cb64e50497'
res['reference']['s261_payload_sha256'] = '26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92'
# S260: the compiled MADT: GICC entries (type 0x0B, length 0x52 for ACPI 6.3), EfficiencyClass at +0x4C
madts = [q for q in (TREE / 'Build').glob('**/MADT_Static*') if q.is_file() and q.suffix in ('.acpi', '.bin', '.aml')]
res['madt_build_files'] = []
for q in madts:
    raw = q.read_bytes()
    i = raw.find(b'APIC')
    eff = []
    if i >= 0:
        at = i + 44
        while at + 2 <= len(raw) and raw[at] != 0 and at < i + int.from_bytes(raw[i + 4:i + 8], 'little'):
            t, l = raw[at], raw[at + 1]
            if t == 0x0b and l >= 0x50:
                eff.append({'mpidr': '%#x' % int.from_bytes(raw[at + 68:at + 76], 'little'), 'efficiency': raw[at + 76]})
            if l == 0:
                break
            at += l
    res['madt_build_files'].append({'path': str(q), 'sha256': sha(raw), 'gicc': eff})
(OUT / ('result-' + a.variant + '.json')).write_text(json.dumps(res, indent=2) + '\n')
print(json.dumps(res, indent=2))
