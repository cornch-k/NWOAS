#!/usr/bin/env python3
"""Build the isolated vGIC SPI-disable mask-polarity candidate."""

from __future__ import annotations

from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess


ROOT = Path('/Volumes/X31/NWOAS')
M1N1 = ROOT / 'm1n1_windows'
SOURCE = M1N1 / 'src/hv_vgic.c'
TARGET = M1N1 / 'build/m1n1-s136-vgic-mask.bin'
OUT = ROOT / 'nwoas_scripts/smp-s136'
NEEDLE = b'#define NWOAS_VGIC_SPI_DISABLE_MASK 0'
REPLACEMENT = b'#define NWOAS_VGIC_SPI_DISABLE_MASK 1'

before = SOURCE.read_bytes()
assert before.count(NEEDLE) == 1
OUT.mkdir(parents=True, exist_ok=True)

try:
    SOURCE.write_bytes(before.replace(NEEDLE, REPLACEMENT, 1))
    env = os.environ.copy()
    result = subprocess.run(
        ['make', f'-j{os.cpu_count() or 1}'], cwd=M1N1, env=env,
        stdout=(OUT / 'build.log').open('w'), stderr=subprocess.STDOUT,
    )
    if result.returncode:
        raise SystemExit(f'build failed {result.returncode}; see {OUT / "build.log"}')
    shutil.copyfile(M1N1 / 'build/m1n1.bin', TARGET)
    data = TARGET.read_bytes()
    manifest = {
        'hypervisor': str(TARGET),
        'bytes': len(data),
        'sha256': hashlib.sha256(data).hexdigest(),
        'base': 'S133 single-owner eight-core hypervisor',
        'change': 'GICD_ICENABLER SPI disable now masks the matching AIC interrupt',
        'upstream_evidence': 'origin/bugfix/vgic_fixes commit 1b3f004b carries the same polarity correction',
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))
finally:
    SOURCE.write_bytes(before)
