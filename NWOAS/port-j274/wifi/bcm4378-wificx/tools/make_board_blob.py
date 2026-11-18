#!/usr/bin/env python3
"""Build nwoas-bcm4378-board.bin from an offline ADT dump of the Mac mini.

Windows has no ADT, so the two per-device values the firmware needs and that
macOS/m1n1 take from /arm-io/wlan are shipped in the driver package:
  - wifi-calibration-msf (2096 bytes on this J274; sent with the "calload" iovar)
  - local-mac-address    (appended to the NVRAM as macaddr=, like wifi_up.py)

File format (parsed by nb_board_parse in src/core/nwbc_core.c):
  "NWWL", u32 version 1, u32 flags (bit0 = MAC present), mac[6], pad[2],
  u32 cal_len, u32 crc32(cal), cal[cal_len]

Same ADT reading as nwoas_scripts/wifi-s257/wifi_up.py wlan_info().
The MAC address is never printed.  The output is private (package only).

usage: make_board_blob.py <adt dump> <out file> [--proxyclient DIR]
"""
import argparse
import hashlib
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('adt', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--proxyclient', type=Path, default=ROOT / 'm1n1_windows' / 'proxyclient')
    ap.add_argument('--expect-cal-len', type=int, default=2096)
    a = ap.parse_args()
    sys.path.insert(0, str(a.proxyclient))
    from m1n1.adt import load_adt
    adt = load_adt(a.adt.read_bytes())
    w = adt['/arm-io/wlan']
    board = str(w.getprop('module-instance') or '').strip('\0').strip()
    cal = w.getprop('wifi-calibration-msf')
    cal = bytes(cal) if cal is not None else b''
    mac = w.getprop('local-mac-address')
    mac = bytes(mac) if mac is not None and len(bytes(mac)) == 6 else None
    if board != 'atlantisb':
        sys.exit(f'STOP module-instance {board!r} is not atlantisb (J274)')
    if len(cal) != a.expect_cal_len:
        sys.exit(f'STOP wifi-calibration-msf is {len(cal)} bytes, expected {a.expect_cal_len}')
    if mac is None:
        sys.exit('STOP no 6-byte local-mac-address in /arm-io/wlan')
    hdr = b'NWWL' + struct.pack('<II', 1, 1) + mac + b'\0\0' + struct.pack('<II', len(cal), zlib.crc32(cal) & 0xFFFFFFFF)
    blob = hdr + cal
    assert len(hdr) == 28
    a.out.write_bytes(blob)
    print(f'board blob {a.out.name}: {len(blob)} bytes, cal {len(cal)} bytes '
          f'sha256(cal)={hashlib.sha256(cal).hexdigest()[:16]}, mac present (not shown), '
          f'sha256(file)={hashlib.sha256(blob).hexdigest()}')


if __name__ == '__main__':
    main()
