#!/usr/bin/env python3
"""S84: add scripts to the identified WINARM2 USB, preserving existing files.

Default is read-only validation. --apply copies the package and backs up replaced
files. It never formats, repartitions, modifies a WIM, or accesses .env.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'nwoas_scripts/setup-repair'
VOLUME = Path('/Volumes/WINARM2')


def validate_info(info):
    if info.get('MountPoint') != str(VOLUME) or info.get('VolumeName') != 'WINARM2':
        raise ValueError('unexpected mount point or volume name')
    if info.get('Internal') is not False or info.get('BusProtocol') != 'USB':
        raise ValueError('target must be an external USB device')
    size = info.get('TotalSize', info.get('DiskSize', 0))
    if not 12_000_000_000 <= size <= 20_000_000_000:
        raise ValueError('size differs from the recorded 15.4 GB installer USB')
    if info.get('Writable') is False or info.get('ReadOnlyVolume') is True:
        raise ValueError('volume is read-only')
    if str(info.get('FilesystemType', '')).lower() not in ('msdos', 'fat32'):
        raise ValueError('expected FAT32 installer media')


def validate_package():
    manifest = json.loads((PACKAGE / 'PACKAGE.json').read_text())
    for entry in manifest['files']:
        rel = Path(entry['path'])
        if rel.is_absolute() or '..' in rel.parts:
            raise ValueError('invalid package path')
        data = (PACKAGE / rel).read_bytes()
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError(f'package hash mismatch: {rel}')
    for name in ('autounattend.xml', 'SCRIPTS/interactive.xml'):
        tree = ET.parse(PACKAGE / name)
        banned = {'DiskConfiguration', 'ImageInstall', 'InstallTo', 'InstallToAvailablePartition',
                  'WillWipeDisk', 'AutoLogon', 'UserAccounts'}
        if any(e.tag.split('}')[-1] in banned for e in tree.iter()):
            raise ValueError('package answer file contains an installation/disk/account directive')
        ns = {'u': 'urn:schemas-microsoft-com:unattend'}
        keys = tree.findall('.//u:ProductKey', ns)
        setup_keys = tree.findall("./u:settings[@pass='windowsPE']/u:component[@name='Microsoft-Windows-Setup']/u:UserData/u:ProductKey", ns)
        if len(keys) != 1 or keys != setup_keys:
            raise ValueError('expected only one Setup interactive ProductKey directive')
        key = keys[0]
        if (len(key) != 2 or key.findtext('u:Key', namespaces=ns) != '00000-00000-00000-00000-00000'
                or key.findtext('u:WillShowUI', namespaces=ns) != 'Always'):
            raise ValueError('only the S85 interactive placeholder with Always UI is allowed')
    return manifest


def image_parts(volume):
    sources = volume / 'sources'
    split = sources / 'install.swm'
    if split.is_file():
        with split.open('rb') as stream:
            header = stream.read(48)
        if len(header) < 48 or header[:8] != b'MSWIM\x00\x00\x00':
            raise ValueError('invalid SWM header')
        part, count = struct.unpack_from('<HH', header, 40)
        if part != 1 or not 1 <= count <= 99:
            raise ValueError('invalid SWM part count')
        files = [split] + [sources / f'install{i}.swm' for i in range(2, count + 1)]
        for i, path in enumerate(files, 1):
            with path.open('rb') as stream:
                other = stream.read(48)
            if len(other) != 48 or other[:8] != header[:8] or other[24:40] != header[24:40]:
                raise ValueError(f'SWM set GUID/header mismatch: {path.name}')
            if struct.unpack_from('<HH', other, 40) != (i, count):
                raise ValueError(f'SWM part sequence mismatch: {path.name}')
        return files
    for name in ('install.wim', 'install.esd'):
        if (sources / name).is_file():
            return [sources / name]
    raise ValueError('installation image missing')


def reject_symlinks(path):
    for item in (path, *path.parents):
        if item.is_symlink():
            raise ValueError(f'symlink in destination: {item}')
        if item == VOLUME:
            break


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    manifest = validate_package()
    info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(VOLUME)]))
    validate_info(info)
    reject_symlinks(VOLUME)
    for relative in ('sources/boot.wim', 'efi/boot/bootaa64.efi'):
        if not (VOLUME / relative).is_file():
            raise ValueError(f'expected ARM64 installation file missing: {relative}')
    images = image_parts(VOLUME)
    print('Validated:', info.get('DeviceIdentifier'), info['VolumeName'])
    print('Image files:', ', '.join(p.name for p in images))
    paths = [entry['path'] for entry in manifest['files']] + ['PACKAGE.json']
    for rel in paths:
        target = VOLUME / rel
        reject_symlinks(target)
        if target.exists() and not target.is_file():
            raise ValueError(f'destination is not a regular file: {rel}')
    if not args.apply:
        print('READ-ONLY PASS; --apply stages scripts without modifying images.')
        return
    backup = VOLUME / ('NWOAS-S84-BACKUP-' + time.strftime('%Y%m%d-%H%M%S'))
    backup.mkdir(exist_ok=False)
    # Scripts first, automatic answer last; keep existing content in the backup.
    paths.sort(key=lambda p: p == 'autounattend.xml')
    for rel in paths:
        source, target = PACKAGE / rel, VOLUME / rel
        if target.exists():
            saved = backup / rel
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, saved)
        target.parent.mkdir(parents=True, exist_ok=True)
        temp = target.with_name(target.name + '.nwoas-s84-tmp')
        with temp.open('xb') as stream:
            stream.write(source.read_bytes())
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, target)
        if target.read_bytes() != source.read_bytes():
            raise ValueError(f'readback mismatch: {rel}')
    report = {'device': info.get('DeviceIdentifier'), 'volume': str(VOLUME),
              'backup': str(backup), 'image_parts': [p.name for p in images],
              'package': manifest, 'status': 'STAGED_NOT_WINPE_VERIFIED'}
    report_path = ROOT / 'nwoas_scripts/logs/setup-s84-staging.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    subprocess.run(['sync'], check=True)
    print('STAGING PASS. Backup:', backup)
    print('WinPE: for %d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do @if exist %d:\\NWOAS-S84.TAG call %d:\\SCRIPTS\\FIX11.CMD')


if __name__ == '__main__':
    main()
