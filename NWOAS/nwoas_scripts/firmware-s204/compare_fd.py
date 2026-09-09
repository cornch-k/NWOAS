#!/usr/bin/env python3
"""S204 step 2: prove the source-built FD differs from the pinned S192 FD in
exactly the two image_size header bytes (0-based offsets 18 and 19).

Both FDs are sliced at 1376256 from their respective payloads.  If anything
other than offsets 18/19 differs, this reports it honestly and does not write a
pass; it never promotes a mismatch.
"""
import hashlib, json, struct
from pathlib import Path

ROOT = Path('/Volumes/X31/NWOAS')
OUT = ROOT / 'nwoas_scripts/firmware-s204'
FD_OFF = 1376256
S192 = ROOT / 'nwoas_scripts/firmware-s192/m1n1-payload-s192-native-rtc-p12.bin'
S192_SHA = 'b67313c48e057e3c1e608521b69a611e129328c04554785e429f4a4211d4c7c9'
S204 = OUT / 'm1n1-payload-s204-native-rtc-p12-30mib-hdr.bin'
PCD_FD_SIZE = 0x01E00000


def sha(b): return hashlib.sha256(b).hexdigest()


s192 = S192.read_bytes()
s204 = S204.read_bytes()
assert sha(s192) == S192_SHA, 'S192 payload hash drifted; refusing to compare'
fd192 = s192[FD_OFF:]
fd204 = s204[FD_OFF:]

diffs = [(i, fd192[i], fd204[i]) for i in range(min(len(fd192), len(fd204))) if fd192[i] != fd204[i]]
expected = {18: (0x0e, 0xe0), 19: (0x10, 0x01)}
offsets = sorted(o for o, _, _ in diffs)
exact = (len(fd192) == len(fd204)
         and offsets == [18, 19]
         and all((o in expected and expected[o] == (a, b)) for o, a, b in diffs))

is192 = struct.unpack('<Q', fd192[16:24])[0]
is204 = struct.unpack('<Q', fd204[16:24])[0]

result = {
    'expected_only_offsets': [18, 19],
    'fd_offset_in_payload': FD_OFF,
    'fd192_len': len(fd192), 'fd204_len': len(fd204),
    'fd192_sha256': sha(fd192), 'fd204_sha256': sha(fd204),
    'diff_offsets_0based': offsets,
    'diffs': [{'offset': o, 's192': a, 's204': b} for o, a, b in diffs],
    'image_size_s192': hex(is192), 'image_size_s204': hex(is204),
    'pcd_fd_size': hex(PCD_FD_SIZE),
    'image_size_s204_eq_pcd': is204 == PCD_FD_SIZE,
    'image_size_s204_ge_fd_len': is204 >= len(fd204),
    'exact_two_byte_header_diff': exact,
}
(OUT / 'fd-compare.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
if not exact:
    raise SystemExit('FD differs beyond the two header bytes; diagnose before promoting')
print('OK: source-built FD differs from S192 FD only at header offsets 18 and 19')
