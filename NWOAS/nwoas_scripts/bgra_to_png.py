#!/usr/bin/env python3
"""Convert a tightly packed or padded BGRA framebuffer dump to RGBA PNG."""

import argparse
import binascii
import struct
import zlib
from pathlib import Path


def chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF)


parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--width", type=int, default=1280)
parser.add_argument("--height", type=int, default=720)
parser.add_argument("--stride", type=int, default=5120)
args = parser.parse_args()

raw = args.source.read_bytes()
needed = args.stride * args.height
if len(raw) != needed:
    raise SystemExit(f"expected {needed} bytes, got {len(raw)}")

scanlines = bytearray()
for y in range(args.height):
    row = raw[y * args.stride:y * args.stride + args.width * 4]
    rgba = bytearray(len(row))
    rgba[0::4] = row[2::4]
    rgba[1::4] = row[1::4]
    rgba[2::4] = row[0::4]
    rgba[3::4] = row[3::4]
    scanlines.append(0)  # PNG filter type None
    scanlines.extend(rgba)

png = bytearray(b"\x89PNG\r\n\x1a\n")
png.extend(chunk(b"IHDR", struct.pack(">IIBBBBB", args.width, args.height, 8, 6, 0, 0, 0)))
png.extend(chunk(b"IDAT", zlib.compress(bytes(scanlines), 9)))
png.extend(chunk(b"IEND", b""))
args.output.write_bytes(png)
print(f"wrote {args.output} ({len(png)} bytes)")
