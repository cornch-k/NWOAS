#!/usr/bin/env python3
"""Build MADT8 + XHC1 hidden + valid SMBIOS CurrentSpeed payload."""

from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path('/Volumes/X31/NWOAS')
MU = ROOT / 'apple_silicon_platforms_mu'
DSDT = MU / 'Platform/MacMini2020Pkg/AcpiTables/DSDT.asl'
SMBIOS = MU / 'Silicon/Apple/AppleSiliconPkg/Drivers/SmbiosInfoDxe/SmbiosInfoDxe.c'
S131_BUILD = ROOT / 'nwoas_scripts/smp-s131/build_candidate.py'
S131_PAYLOAD = ROOT / 'm1n1_windows/m1n1-payload-s131-8cpu.bin'
S131_MANIFEST = ROOT / 'nwoas_scripts/smp-s131/manifest.json'
TARGET = ROOT / 'm1n1_windows/m1n1-payload-s142-8cpu-usba-speed.bin'
OUT = ROOT / 'nwoas_scripts/smbios-s142'

dsdt_before = DSDT.read_bytes()
smbios_before = SMBIOS.read_bytes()
s131_before = S131_PAYLOAD.read_bytes()
manifest_before = S131_MANIFEST.read_bytes()

device = b'''        Device (XHC1) {
            Name (_HID, "PNP0D15")'''
visible = b'''            Method (_STA) {
                Return (0xF)
            }'''
hidden = b'''            Method (_STA) {
                // S142: keep the HSE-prone SoC USB-C controller isolated.
                Return (Zero)
            }'''
assert dsdt_before.count(device) == 1
start = dsdt_before.index(device)
end = dsdt_before.index(b'        }', dsdt_before.index(visible, start)) + len(b'        }')
segment = dsdt_before[start:end]
assert segment.count(visible) == 1
dsdt_candidate = dsdt_before[:start] + segment.replace(visible, hidden, 1) + dsdt_before[end:]

speed_zero = b'    0,                  // CurrentSpeed;'
speed_valid = b'    3228,               // CurrentSpeed; S142 reporting value'
assert smbios_before.count(speed_zero) == 1
smbios_candidate = smbios_before.replace(speed_zero, speed_valid, 1)

OUT.mkdir(parents=True, exist_ok=True)
try:
    DSDT.write_bytes(dsdt_candidate)
    SMBIOS.write_bytes(smbios_candidate)
    result = subprocess.run(['python3', str(S131_BUILD)], cwd=ROOT)
    if result.returncode:
        raise SystemExit(result.returncode)
    payload = S131_PAYLOAD.read_bytes()
    TARGET.write_bytes(payload)
    manifest = {
        'payload': str(TARGET),
        'bytes': len(payload),
        'sha256': hashlib.sha256(payload).hexdigest(),
        'base': 'S131 MADT8 SSD-first payload',
        'changes': ['XHC1 _STA=0', 'SMBIOS Type 4 CurrentSpeed=3228 MHz'],
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))
finally:
    DSDT.write_bytes(dsdt_before)
    SMBIOS.write_bytes(smbios_before)
    S131_PAYLOAD.write_bytes(s131_before)
    S131_MANIFEST.write_bytes(manifest_before)
