#!/usr/bin/env python3
from pathlib import Path

root = Path('/Volumes/X31/NWOAS')
source = (root / 'apple_silicon_platforms_mu/Silicon/Apple/AppleSiliconPkg/Drivers/SmbiosInfoDxe/SmbiosInfoDxe.c').read_text()
builder = (root / 'nwoas_scripts/smp-s135/build_candidate.py').read_text()

assert source.count('0,                  // CurrentSpeed;') == 1
assert '3228,               // CurrentSpeed; NWOAS S135' in builder
assert 'SMBIOS.write_bytes(source_before)' in builder
assert 'S131_PAYLOAD.write_bytes(stable_payload)' in builder
print('S135 source/restore checks PASS')
