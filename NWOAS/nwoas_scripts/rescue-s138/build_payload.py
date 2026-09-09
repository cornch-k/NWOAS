#!/usr/bin/env python3
"""Build one-core SSD-first UEFI with the unstable SoC USB-C xHCI hidden."""

from pathlib import Path
import hashlib
import json
import subprocess


ROOT = Path('/Volumes/X31/NWOAS')
MU = ROOT / 'apple_silicon_platforms_mu'
DSDT = MU / 'Platform/MacMini2020Pkg/AcpiTables/DSDT.asl'
S129 = ROOT / 'm1n1_windows/m1n1-payload-s129-ssd-first.bin'
TARGET = ROOT / 'm1n1_windows/m1n1-payload-s138-usba-rescue.bin'
OUT = ROOT / 'nwoas_scripts/rescue-s138'
BUILDER = ROOT / 'nwoas_scripts/uefi-s129/build_candidate.py'

source = DSDT.read_bytes()
stable = S129.read_bytes()
needle = b'''        Device (XHC1) {
            Name (_HID, "PNP0D15")'''
assert source.count(needle) == 1
sta = b'''            Method (_STA) {
                Return (0xF)
            }'''
hidden = b'''            Method (_STA) {
                // S138 rescue: keep the HSE-prone SoC USB-C xHCI out of Windows.
                // USB-A FL1100 remains available for the keyboard and NWOS bootstrap.
                Return (Zero)
            }'''
start = source.index(needle)
end = source.index(b'        }', source.index(sta, start)) + len(b'        }')
segment = source[start:end]
assert segment.count(sta) == 1
candidate = source[:start] + segment.replace(sta, hidden, 1) + source[end:]
OUT.mkdir(parents=True, exist_ok=True)

try:
    DSDT.write_bytes(candidate)
    result = subprocess.run(['python3', str(BUILDER)], cwd=ROOT)
    if result.returncode:
        raise SystemExit(result.returncode)
    payload = S129.read_bytes()
    TARGET.write_bytes(payload)
    manifest = {
        'payload': str(TARGET),
        'bytes': len(payload),
        'sha256': hashlib.sha256(payload).hexdigest(),
        'base': 'S129 one-core SSD-first payload',
        'change': 'XHC1 _STA=0; retain USB-A FL1100 for stable recovery input',
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))
finally:
    DSDT.write_bytes(source)
    S129.write_bytes(stable)
