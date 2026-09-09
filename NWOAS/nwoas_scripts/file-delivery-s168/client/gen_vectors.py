#!/usr/bin/env python3
"""Emit vectors.h from the REAL ../channel.py so the host test cross-checks the
C wire logic against the authoritative Python state machine. Read-only: imports
channel.py, never edits it or anything outside this client directory."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))  # parent dir holds channel.py
import channel  # noqa: E402


def carr(name, data):
    body = ",".join(str(b) for b in data)
    return "static const unsigned char %s[%d] = {%s};\n" % (name, len(data), body)


def main():
    token = bytes(range(0x10, 0x20))            # 16 distinct bytes
    artifact = bytes((i * 37 + 5) & 0xFF for i in range(3 * channel.PAYLOAD_CAP + 123))
    ch = channel.HostArtifactChannel(token).register(artifact)

    out = ["/* AUTO-GENERATED from ../channel.py by gen_vectors.py. Do not edit. */\n",
           "#ifndef NWOAS_VECTORS_H\n#define NWOAS_VECTORS_H\n\n"]
    out.append(carr("V_TOKEN", token))
    out.append("static const unsigned long long V_FILE_SIZE = %dULL;\n" % ch.file_size)
    out.append(carr("V_SHA", ch.sha256))

    # manifest window (id 1)
    assert ch.handle_request(_req(channel, token, 1, channel.SENTINEL, 0))
    out.append(carr("V_MANIFEST", ch.response_window()))

    # a full-cap data chunk (id 2, offset 0)
    assert ch.handle_request(_req(channel, token, 2, 0, channel.PAYLOAD_CAP))
    out.append(carr("V_DATA", ch.response_window()))
    out.append("static const unsigned long long V_DATA_OFF = 0ULL;\n")
    out.append("static const unsigned V_DATA_LEN = %dU;\n" % channel.PAYLOAD_CAP)

    # final-short chunk near EOF (id 3): request full cap, get remainder
    final_off = 3 * channel.PAYLOAD_CAP
    assert ch.handle_request(_req(channel, token, 3, final_off, channel.PAYLOAD_CAP))
    out.append(carr("V_FINAL", ch.response_window()))
    out.append("static const unsigned long long V_FINAL_OFF = %dULL;\n" % final_off)
    out.append("static const unsigned V_FINAL_LEN = %dU;\n" % channel.PAYLOAD_CAP)
    out.append("static const unsigned V_FINAL_SERVED = %dU;\n" % (ch.file_size - final_off))

    out.append("\n#endif\n")
    with open(os.path.join(HERE, "vectors.h"), "w") as f:
        f.write("".join(out))
    print("wrote vectors.h (file_size=%d)" % ch.file_size)


def _req(mod, token, rid, offset, length):
    import struct
    import zlib
    b = bytearray(mod.BLOCK)
    b[0:16] = mod.REQ_MAGIC
    b[16:32] = token
    struct.pack_into("<IQI", b, 32, rid, offset, length)
    struct.pack_into("<I", b, 48, zlib.crc32(bytes(b[0:48])))
    return bytes(b)


if __name__ == "__main__":
    main()
