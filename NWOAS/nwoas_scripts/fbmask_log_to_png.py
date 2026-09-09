#!/usr/bin/env python3
"""Decode the D56 320x180 framebuffer mask from a serial log."""

import argparse
import binascii
import re
import struct
import zlib
from pathlib import Path


def chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF)


parser = argparse.ArgumentParser()
parser.add_argument("log", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()

data = args.log.read_bytes().replace(b"\x00", b"")
rows = {}
for match in re.finditer(rb"HVLOG: FBMASK y=(\d{3}) ([0-9a-f]{80})", data):
    rows[int(match.group(1))] = bytes.fromhex(match.group(2).decode())
if set(rows) != set(range(180)):
    missing = sorted(set(range(180)) - set(rows))
    raise SystemExit(f"incomplete mask: rows={len(rows)}, missing={missing[:12]}")

scanlines = bytearray()
for y in range(180):
    scanlines.append(0)
    for x in range(320):
        marked = (rows[y][x // 8] >> (7 - x % 8)) & 1
        scanlines.extend((255, 255, 255, 255) if marked else (24, 0, 82, 255))

png = bytearray(b"\x89PNG\r\n\x1a\n")
png.extend(chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 180, 8, 6, 0, 0, 0)))
png.extend(chunk(b"IDAT", zlib.compress(bytes(scanlines), 9)))
png.extend(chunk(b"IEND", b""))
args.output.write_bytes(png)
print(f"wrote {args.output} ({len(png)} bytes)")
