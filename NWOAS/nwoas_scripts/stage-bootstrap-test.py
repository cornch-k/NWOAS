#!/usr/bin/env python3
"""S6-P0 staging: verify removable USB identity, then stage recovery artifacts.
[DESIGN] PASS = hashes match on USB; never means boot success.
Safety: differing files are refused except the exact known installer in repair mode.
No erase/eject/reboot/kmutil/password use.
"""
import hashlib
from pathlib import Path
import plistlib
import subprocess
import sys
import os
import tempfile

ROOT = Path('/Volumes/X31/NWOAS')
USB = Path('/Volumes/USB')
SOURCE = ROOT / 'recovery-bootstrap-20260906'
REPAIR = sys.argv[1:] == ['--repair-tahoe-md5']
DIAG = REPAIR or sys.argv[1:] == ['--tahoe-afk-diag']
if sys.argv[1:] and not DIAG:
    raise SystemExit('Only --tahoe-afk-diag or --repair-tahoe-md5 is accepted')
DEST = USB / 'NWOAS-Tahoe-D1' if DIAG else USB
if DIAG:
    SOURCE = ROOT / 'recovery-tahoe-afk-20260906'

def info(path):
    return plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(path)]))

def validate_usb():
    volume = info(USB)
    assert volume['VolumeUUID'] == 'F2A26E85-22CF-409E-A49A-96F02CE38FEE'
    assert volume['MountPoint'] == str(USB) and volume['VolumeName'] == 'USB'
    assert volume['RemovableMedia'] and not volume['Internal'] and volume['Writable']
    store = volume['APFSPhysicalStores'][0]['APFSPhysicalStore']
    physical = info(info(store)['ParentWholeDisk'])
    assert physical['BusProtocol'] == 'USB' and not physical['Internal']
    assert physical['RemovableMedia'] and 30_000_000_000 < physical['TotalSize'] < 32_000_000_000

validate_usb()
names = ['m1n1-bare.bin', 'm1n1-bare-GOOD.bin', 'm1n1-bare-prev.bin', 'install-in-recovery.sh']
assert not DEST.is_symlink()
if DIAG:
    names.append('README.md')
    DEST.mkdir(exist_ok=True)
for name in names:
    data = (SOURCE / name).read_bytes()
    if DIAG and name == 'm1n1-bare.bin':
        assert hashlib.sha1(data).hexdigest() == 'dd8a02bf19ed25e6d8cb33392e56aea4fb58306f'
    elif name.endswith('.bin'):
        assert hashlib.md5(data).hexdigest() == 'b64269a0fe20e7118028b00e8b5b65f0'
    target = DEST / name
    assert not target.is_symlink(), f'Refusing symlink: {target}'
    if target.exists():
        existing = target.read_bytes()
        if existing != data:
            assert REPAIR and name == 'install-in-recovery.sh', f'Refusing overwrite: {target}'
            assert hashlib.sha256(existing).hexdigest() == '51dc392abaa61ee983d40fe385aa251f03e015c2f9ac4f5ddf5a08d8526bece6'
            expected = existing.replace(b'shasum -a 1', b'md5 -q').replace(b' | cut -d " " -f 1', b'').replace(
                b'dd8a02bf19ed25e6d8cb33392e56aea4fb58306f', b'24138aed6985ac1f54b249878c58e22a')
            assert data == expected, 'Unexpected repair content'
for name in names:
    validate_usb()
    data = (SOURCE / name).read_bytes()
    target = DEST / name
    if REPAIR and target.exists() and target.read_bytes() != data:
        assert name == 'install-in-recovery.sh'
        old = target.read_bytes()
        assert hashlib.sha256(old).hexdigest() == '51dc392abaa61ee983d40fe385aa251f03e015c2f9ac4f5ddf5a08d8526bece6'
        backup = DEST / 'install-in-recovery.sh.before-md5'
        assert not backup.is_symlink()
        if backup.exists():
            assert backup.read_bytes() == old
        else:
            with backup.open('xb') as output:
                output.write(old)
        with tempfile.NamedTemporaryFile(dir=DEST, prefix='.installer-', delete=False) as output:
            temporary = output.name
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        subprocess.run(['bash', '-n', temporary], check=True)
        os.replace(temporary, target)
    if not target.exists():
        with target.open('xb') as output:
            output.write(data)
    assert target.read_bytes() == data
    print('[FACT] STAGED', name, 'sha256=' + hashlib.sha256(data).hexdigest())
subprocess.run(['sync'], check=True)
print('Staging verified. No boot operation performed.')
