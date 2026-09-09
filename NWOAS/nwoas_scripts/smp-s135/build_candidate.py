#!/usr/bin/env python3
"""Build S135: S133's 8-core UEFI with valid SMBIOS CurrentSpeed."""

from __future__ import annotations

from pathlib import Path
import hashlib
import json
import subprocess


ROOT = Path("/Volumes/X31/NWOAS")
MU = ROOT / "apple_silicon_platforms_mu"
S131_BUILD = ROOT / "nwoas_scripts/smp-s131/build_candidate.py"
S131_PAYLOAD = ROOT / "m1n1_windows/m1n1-payload-s131-8cpu.bin"
TARGET = ROOT / "m1n1_windows/m1n1-payload-s135-8cpu-speed.bin"
OUT = ROOT / "nwoas_scripts/smp-s135"
SMBIOS = MU / "Silicon/Apple/AppleSiliconPkg/Drivers/SmbiosInfoDxe/SmbiosInfoDxe.c"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


source_before = SMBIOS.read_bytes()
stable_payload = S131_PAYLOAD.read_bytes()
needle = b"    0,                  // CurrentSpeed;"
replacement = b"    3228,               // CurrentSpeed; NWOAS S135: match MaxSpeed for Windows reporting"
assert source_before.count(needle) == 1
assert len(stable_payload) == 32342016

OUT.mkdir(parents=True, exist_ok=True)
try:
    SMBIOS.write_bytes(source_before.replace(needle, replacement, 1))
    result = subprocess.run(["python3", str(S131_BUILD)], cwd=ROOT)
    if result.returncode:
        raise SystemExit(result.returncode)
    candidate = S131_PAYLOAD.read_bytes()
    TARGET.write_bytes(candidate)
    manifest = {
        "payload": str(TARGET),
        "sha256": sha256(candidate),
        "bytes": len(candidate),
        "base": "S131/S133 MADT8 SSD-first UEFI payload",
        "change": "SMBIOS Type 4 CurrentSpeed 0 -> 3228 MHz; no CPU control or ACPI _CPC change",
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
finally:
    SMBIOS.write_bytes(source_before)
    S131_PAYLOAD.write_bytes(stable_payload)
