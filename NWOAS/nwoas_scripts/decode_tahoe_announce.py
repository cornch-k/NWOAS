"""Decode the observed Tahoe disp0 announcement; NOT a command encoder.

Field positions/values are measured. Meanings of the short header fields and
record tag are still unknown; this parser intentionally accepts only the
observed announcement shape, not an assumed general EPIC v4 protocol.
"""
from pathlib import Path
import json
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'm1n1_windows/proxyclient'))
from m1n1.fw.common import OSSerialize

def decode(frame):
    if len(frame) < 72:
        raise ValueError('Truncated announcement')
    magic, size, channel, queue_type = struct.unpack_from('<4sIII', frame)
    if magic != b'IOP ' or size != len(frame) - 16:
        raise ValueError('Queue frame length/magic mismatch')
    first, second, third, length = struct.unpack_from('<BBHI', frame, 16)
    if first != 4 or second not in (11, 75) or third != 3 or length != size - 8:
        raise ValueError('Not the observed Tahoe announcement format')
    unknown64, tag, record_version = struct.unpack_from('<QII', frame, 24)
    name = frame[40:72].split(b'\0', 1)[0].decode('ascii')
    properties = OSSerialize().parse(frame[72:])
    return dict(queue_channel=channel, queue_type=queue_type,
                header_bytes=[first, second, third], length_field=length,
                unknown64=hex(unknown64), record_tag=hex(tag),
                record_version=record_version, name=name, properties=properties)

if __name__ == '__main__':
    print(json.dumps(decode(Path(sys.argv[1]).read_bytes()), indent=2))
