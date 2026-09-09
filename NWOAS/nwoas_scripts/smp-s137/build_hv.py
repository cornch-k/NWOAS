#!/usr/bin/env python3
"""Build S137: S133 behavior plus read-only SPI-disable tracing."""

from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess


ROOT = Path('/Volumes/X31/NWOAS')
M1N1 = ROOT / 'm1n1_windows'
SOURCE = M1N1 / 'src/hv_vgic.c'
TARGET = M1N1 / 'build/m1n1-s137-vgic-trace.bin'
OUT = ROOT / 'nwoas_scripts/smp-s137'
NEEDLE = b'#define NWOAS_VGIC_SPI_DISABLE_TRACE 0'
REPLACEMENT = b'#define NWOAS_VGIC_SPI_DISABLE_TRACE 1'

before = SOURCE.read_bytes()
assert before.count(NEEDLE) == 1
assert before.count(b'#define NWOAS_VGIC_SPI_DISABLE_MASK 0') == 1
OUT.mkdir(parents=True, exist_ok=True)

try:
    SOURCE.write_bytes(before.replace(NEEDLE, REPLACEMENT, 1))
    with (OUT / 'build.log').open('w') as output:
        result = subprocess.run(
            ['make', f'-j{os.cpu_count() or 1}'], cwd=M1N1,
            stdout=output, stderr=subprocess.STDOUT,
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
        'change': 'trace effective GICD_ICENABLER SPI transitions; retain S133 AIC behavior',
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))
finally:
    SOURCE.write_bytes(before)
