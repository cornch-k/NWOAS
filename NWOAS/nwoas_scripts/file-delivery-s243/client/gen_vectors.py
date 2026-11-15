#!/usr/bin/env python3
"""Emit vectors.h from the REAL ../s243_channel.py so the host test cross-checks
the C wire logic (and SHA-256) against the authoritative Python state machine."""
import hashlib
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import s243_channel as channel  # noqa: E402


def carr(name, data):
    return "static const unsigned char %s[%d] = {%s};\n" % (name, len(data), ",".join(str(b) for b in data))


def req(token, rid, index, offset, length):
    b = bytearray(channel.BLOCK)
    b[0:16] = channel.REQ_MAGIC
    b[16:32] = token
    struct.pack_into("<IQII", b, 32, rid, offset, length, index)
    struct.pack_into("<I", b, 52, zlib.crc32(bytes(b[0:52])))
    return bytes(b)


def main():
    token = bytes(range(0x10, 0x20))
    a0 = b"first artifact"
    a1 = bytes((i * 37 + 5) & 0xFF for i in range(3 * channel.PAYLOAD_CAP + 123))
    ch = channel.HostArtifactChannel(token).register([("A0.INF", a0), ("big.sys", a1)])
    out = ["/* AUTO-GENERATED from ../s243_channel.py by gen_vectors.py. Do not edit. */\n",
           "#ifndef NWOAS_VECTORS243_H\n#define NWOAS_VECTORS243_H\n"]
    out.append(carr("V_TOKEN", token))
    out.append(carr("V_NONE", ch.response_window()[:256]))
    out.append("static const unsigned long long V_FILE_SIZE = %dULL;\n" % len(a1))
    out.append(carr("V_SHA", hashlib.sha256(a1).digest()))
    assert ch.handle_request(req(token, 1, 1, channel.SENTINEL, 0))
    out.append(carr("V_MANIFEST", ch.response_window()))
    assert ch.handle_request(req(token, 2, 1, 0, channel.PAYLOAD_CAP))
    out.append(carr("V_DATA", ch.response_window()))
    out.append("static const unsigned V_DATA_LEN = %dU;\n" % channel.PAYLOAD_CAP)
    final_off = 3 * channel.PAYLOAD_CAP
    assert ch.handle_request(req(token, 3, 1, final_off, channel.PAYLOAD_CAP))
    out.append(carr("V_FINAL", ch.response_window()))
    out.append("static const unsigned long long V_FINAL_OFF = %dULL;\n" % final_off)
    out.append("static const unsigned V_FINAL_SERVED = %dU;\n" % (len(a1) - final_off))
    # golden request block the C builder must reproduce byte-for-byte
    out.append(carr("V_REQ", req(token, 7, 5, 65408, channel.PAYLOAD_CAP)))
    # SHA-256 vectors: empty, "abc", 1,000,000 x 'a' is done in C; a1 above
    out.append(carr("V_SHA_ABC", hashlib.sha256(b"abc").digest()))
    out.append(carr("V_SHA_EMPTY", hashlib.sha256(b"").digest()))
    out.append(carr("V_SHA_MILLION_A", hashlib.sha256(b"a" * 1000000).digest()))
    out.append("\n#endif\n")
    with open(os.path.join(HERE, "vectors.h"), "w") as f:
        f.write("".join(out))
    print("wrote vectors.h")


if __name__ == "__main__":
    main()
