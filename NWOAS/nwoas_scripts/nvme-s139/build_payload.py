#!/usr/bin/env python3
"""Build the S131 eight-core payload with unstable SoC XHC1 hidden."""

from pathlib import Path
import hashlib
import json
import subprocess


ROOT = Path("/Volumes/X31/NWOAS")
REPO = ROOT / "apple_silicon_platforms_mu"
DSDT = REPO / "Platform/MacMini2020Pkg/AcpiTables/DSDT.asl"
S131 = ROOT / "m1n1_windows/m1n1-payload-s131-8cpu.bin"
S131_MANIFEST = ROOT / "nwoas_scripts/smp-s131/manifest.json"
TARGET = ROOT / "m1n1_windows/m1n1-payload-s139-8cpu-usba.bin"
OUT = ROOT / "nwoas_scripts/nvme-s139"
BUILDER = ROOT / "nwoas_scripts/smp-s131/build_candidate.py"

source = DSDT.read_bytes()
stable_s131 = S131.read_bytes()
stable_s131_manifest = S131_MANIFEST.read_bytes()
device = b'''        Device (XHC1) {
            Name (_HID, "PNP0D15")'''
visible = b'''            Method (_STA) {
                Return (0xF)
            }'''
hidden = b'''            Method (_STA) {
                // S139: isolate the known HSE-prone SoC USB-C controller while
                // validating the eight-core NVMe completion fix.
                Return (Zero)
            }'''

assert source.count(device) == 1
start = source.index(device)
end = source.index(b"        }", source.index(visible, start)) + len(b"        }")
segment = source[start:end]
assert segment.count(visible) == 1
candidate = source[:start] + segment.replace(visible, hidden, 1) + source[end:]
OUT.mkdir(parents=True, exist_ok=True)

try:
    DSDT.write_bytes(candidate)
    result = subprocess.run(["python3", str(BUILDER)], cwd=ROOT)
    if result.returncode:
        raise SystemExit(result.returncode)
    payload = S131.read_bytes()
    TARGET.write_bytes(payload)
    manifest = {
        "payload": str(TARGET),
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "base": "S131 eight-core SSD-first payload",
        "change": "XHC1 _STA=0; retain FL1100 USB-A while testing S139 NVMe DPC fix",
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
finally:
    DSDT.write_bytes(source)
    S131.write_bytes(stable_s131)
    S131_MANIFEST.write_bytes(stable_s131_manifest)
