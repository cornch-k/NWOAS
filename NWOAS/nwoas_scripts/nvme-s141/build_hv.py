#!/usr/bin/env python3
"""Build an isolated 1 MiB ANS direct-transfer candidate."""

from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path('/Volumes/X31/NWOAS')
M1N1 = ROOT / 'm1n1_windows'
SOURCE = M1N1 / 'src/nvme.c'
TARGET = M1N1 / 'build/m1n1-s141-1m-nvme.bin'
OUT = ROOT / 'nwoas_scripts/nvme-s141'
NEEDLE = b'#define NVME_MAX_BLOCKS 16'
REPLACEMENT = b'#define NVME_MAX_BLOCKS 256'

before = SOURCE.read_bytes()
assert before.count(NEEDLE) == 1
OUT.mkdir(parents=True, exist_ok=True)
try:
    SOURCE.write_bytes(before.replace(NEEDLE, REPLACEMENT, 1))
    with (OUT / 'build.log').open('w') as log:
        result = subprocess.run(
            ['make', f'-j{os.cpu_count() or 1}'], cwd=M1N1,
            stdout=log, stderr=subprocess.STDOUT,
        )
    if result.returncode:
        raise SystemExit(f'build failed {result.returncode}; see {OUT / "build.log"}')
    shutil.copyfile(M1N1 / 'build/m1n1.bin', TARGET)
    data = TARGET.read_bytes()
    manifest = {
        'hypervisor': str(TARGET),
        'bytes': len(data),
        'sha256': hashlib.sha256(data).hexdigest(),
        'base': 'S133 single-owner eight-core hypervisor plus S140 CPU P-state module',
        'change': 'raise direct ANS PRP transfer ceiling from 16 to 256 4-KiB pages',
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))
finally:
    if SOURCE.read_bytes() != before.replace(NEEDLE, REPLACEMENT, 1):
        raise RuntimeError('concurrent nvme.c edit detected; source not restored')
    SOURCE.write_bytes(before)
