#!/usr/bin/env python3
"""S204 step 3: assemble the image-header-corrected candidate.

Recipe is identical to loader-s197 (compat m1n1 prefix + pinned DTB + FD), but
the FD is the S204 source-built one (image_size = 0x01E00000) instead of the
S192 FD.  Read-only with respect to loader-s197: its m1n1.bin, DTB and
validators are consumed, never modified or overwritten.

    payload = loader-s197/out/compat/m1n1.bin      (2,146,304 B, hash-pinned)
            + m1n1_windows/apple-j274-padded.dtb    (65,536 B, hash-pinned)
            + S204 FD                                (30,965,760 B, source-built)

Build-only.  Nothing here boots or touches hardware, launchers, USB, guest/m1n1
main source, secrets or UI, and no ADT/NVRAM dump is read.  No native-driver or
standalone-boot completion is claimed.
"""
import hashlib, json, struct
from pathlib import Path

ROOT = Path('/Volumes/X31/NWOAS')
OUT = ROOT / 'nwoas_scripts/firmware-s204'

M1N1 = ROOT / 'nwoas_scripts/loader-s197/out/compat/m1n1.bin'
M1N1_SHA = '1c255a7f325a125040ed7aa5fcbc8bfda8b82c84d727698d4bf14e97ab162078'
DTB = ROOT / 'm1n1_windows/apple-j274-padded.dtb'
DTB_SHA = 'ecc93b24740d80007d31986a43983c36eef47dd8b0b105b663c25a7b51190f76'
S204_PAYLOAD = OUT / 'm1n1-payload-s204-native-rtc-p12-30mib-hdr.bin'
FD_OFF = 1376256
FD_SHA = 'e3407c3687b2b860bc168f050acdfd686ac0fbc73fe5345fc52ebaf86640cb57'
PCD_FD_SIZE = 0x01E00000
TOTAL = 33177600


def sha(b): return hashlib.sha256(b).hexdigest()


checks = []
def check(name, ok, detail=''):
    checks.append({'check': name, 'ok': bool(ok), 'detail': detail})
    print(('PASS ' if ok else 'FAIL ') + name + (': ' + detail if detail else ''))
    return ok


m1n1 = M1N1.read_bytes()
dtb = DTB.read_bytes()
fd = S204_PAYLOAD.read_bytes()[FD_OFF:]

check('S197 compat m1n1 prefix sha256 pinned (read-only)', sha(m1n1) == M1N1_SHA, sha(m1n1)[:16])
check('pinned DTB sha256', sha(dtb) == DTB_SHA, sha(dtb)[:16])
check('DTB is 64 KiB and FDT magic', len(dtb) == 0x10000 and dtb[:4] == b'\xd0\x0d\xfe\xed')
check('S204 source-built FD sha256', sha(fd) == FD_SHA, sha(fd)[:16])
check('FD magic ARM\\x64 at 0x38', fd[0x38:0x3C] == b'ARM\x64')

code0, code1, text_off, image_size = struct.unpack('<IIQQ', fd[:24])
check('FD Linux header adr x1,. / b 0x8000 / text_offset 0x80000',
      code0 == 0x10000001 and code1 == 0x14001FFF and text_off == 0x80000,
      f'{code0:08x} {code1:08x} text_off=0x{text_off:x}')
check('image_size corrected to 0x01E00000 (30 MiB == PcdFdSize)', image_size == PCD_FD_SIZE,
      f'0x{image_size:x}')
check('image_size >= FD length', image_size >= len(fd), f'0x{image_size:x} >= 0x{len(fd):x}')
check('image_size >= PcdFdSize (0x1E00000)', image_size >= PCD_FD_SIZE, f'0x{image_size:x}')

payload = m1n1 + dtb + fd
dtb_off = len(m1n1)
fd_off = dtb_off + len(dtb)
check('assembled layout m1n1 | dtb | fd',
      payload[dtb_off:dtb_off + 4] == b'\xd0\x0d\xfe\xed' and payload[fd_off + 0x38:fd_off + 0x3C] == b'ARM\x64',
      f'dtb@0x{dtb_off:x} fd@0x{fd_off:x}')
check('total payload == 33177600 B', len(payload) == TOTAL, str(len(payload)))
check('total payload 16 KiB aligned', len(payload) % 0x4000 == 0)

ok = all(c['ok'] for c in checks)
name = 'm1n1-payload-s204-imghdr-compat.bin'
target = OUT / name
if ok:
    target.write_bytes(payload)

heap_note = (
    'Heap-position consequence: m1n1 load_kernel() (src/payload.c) reserves and memcpy()s '
    'image_size bytes for a non-2MiB-aligned inline FD, and heapblock.c advances heap_base by '
    'that amount.  S192/S197 used image_size=0x100E0000, reserving 256.875 MiB; this candidate uses '
    '0x01E00000 (30 MiB == PcdFdSize), so every later m1n1 allocation (kboot_prepare_dt buffers, '
    'malloc arena) sits 0xE2E0000 (~226.875 MiB) lower.  UEFI only requires image_size >= PcdFdSize '
    '(it treats [FdBase, FdBase+PcdFdSize) as its own and is position-independent, relocated in '
    'place, with scratch memory placed below FdBase), and both checks above hold.  The layout '
    'shift is real and has no offline proof of neutrality; it must be confirmed by a '
    'user-run hardware A/B against S197-compat before any launcher is pointed at this FD.'
)

manifest = {
    'candidate': str(target) if ok else None,
    'validated': ok,
    'boot_tested': False,
    'status': 'build-only; awaiting main hardware test',
    'claim': ('offline layout/provenance validated only; no boot, no native-driver, and no '
              'standalone-boot completion is claimed'),
    'bytes': len(payload),
    'sha256': sha(payload) if ok else None,
    'source_patch': str(OUT / 'image_size.patch'),
    'recipe': 'loader-s197 compat recipe with the S204 source-built FD substituted for the S192 FD',
    'layout': {
        'm1n1': {'offset': 0, 'size': len(m1n1), 'sha256': sha(m1n1), 'source': str(M1N1),
                 'note': 'loader-s197 out/compat m1n1.bin, unchanged'},
        'dtb': {'offset': dtb_off, 'size': len(dtb), 'sha256': sha(dtb), 'source': str(DTB)},
        'fd': {'offset': fd_off, 'size': len(fd), 'sha256': sha(fd),
               'source': f'{S204_PAYLOAD}[{FD_OFF}:]', 'image_size': hex(image_size),
               'built_from': 'source (MacMini2020.fdf image_size corrected); not a binary patch'},
    },
    'image_size_constraints': {
        'image_size': hex(image_size), 'pcd_fd_size': hex(PCD_FD_SIZE), 'fd_len': hex(len(fd)),
        'image_size_ge_fd_len': image_size >= len(fd), 'image_size_ge_pcd_fd_size': image_size >= PCD_FD_SIZE,
    },
    'heap_position_consequence': heap_note,
    'checks': checks,
}
(OUT / 'candidate-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(('CANDIDATE ' + str(target) + ' ' + manifest['sha256']) if ok else 'NOT WRITTEN: validation failed')
raise SystemExit(0 if ok else 1)
