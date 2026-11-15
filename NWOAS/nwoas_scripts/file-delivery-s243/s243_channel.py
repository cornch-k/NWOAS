"""S243: multi-artifact host delivery channel (pure logic, no I/O).

Generalization of file-delivery-s168/channel.py. Same geometry, same safety
model, but the host registers an ordered, frozen LIST of named artifacts once
at launch and the guest selects one by index. The guest still never supplies a
host path: a request carries only {id, index, offset, length}.

    LBA 127       request port   guest WRITE, exactly 1 block (4096 B)
    LBA 128       existing job/stdout mailbox -- NOT touched by this channel
    LBA 129..144  response window   guest READ, up to 16 blocks (64 KiB)

Request (4096 B):
    0  16 magic  b'NWOAS-S243-REQ!!'
   16  16 token  boot session token
   32   4 id     u32 >= 1, strictly increasing (identical retry is idempotent)
   36   8 offset u64 (SENTINEL => manifest request for `index`)
   44   4 length u32
   48   4 index  u32, 0-based artifact index (< count)
   52   4 crc32  over [0:52]
   56 ..  MUST be zero

Response window header (128 B), then payload:
    0  16 magic  b'NWOAS-S243-FILE!'
   16  16 token
   32   4 id          last accepted request id (0 before any request)
   36   4 status      0=data 1=final-short 2=no-request 3=manifest
   40   8 file_size   size of artifact `index`
   48   8 offset      echo (SENTINEL for manifest)
   56   4 served      payload bytes in this window
   60   4 payload_crc crc32(payload[0:served])
   64  32 sha256      full digest of artifact `index`
   96   4 index       echo
  100   4 count       number of registered artifacts
  104  20 reserved    zero
  124   4 header_crc  crc32(header[0:124])
  128 ..  payload (manifest: the artifact's ASCII name)
"""
import hashlib
import struct
import zlib

BLOCK = 4096
REQ_LBA = 127
REQ_BLOCKS = 1
PORT_LBA = 128
RESP_LBA = 129
WINDOW_BLOCKS = 16

REQ_MAGIC = b'NWOAS-S243-REQ!!'
RESP_MAGIC = b'NWOAS-S243-FILE!'
assert len(REQ_MAGIC) == 16 and len(RESP_MAGIC) == 16

HEADER = 128
WINDOW_BYTES = WINDOW_BLOCKS * BLOCK
PAYLOAD_CAP = WINDOW_BYTES - HEADER
assert WINDOW_BYTES == 65536 and PAYLOAD_CAP == 65408

SENTINEL = 0xFFFFFFFFFFFFFFFF
MAX_ARTIFACTS = 64
MAX_NAME = 255

STATUS_DATA = 0
STATUS_FINAL = 1
STATUS_NONE = 2
STATUS_MANIFEST = 3


def valid_name(name):
    """Informational artifact name: printable ASCII, no path separators."""
    if not isinstance(name, str) or not 1 <= len(name) <= MAX_NAME:
        return False
    if any(not (0x20 < ord(c) < 0x7f) or c in '\\/:*?"<>|' for c in name):
        return False
    return name not in ('.', '..')


class HostArtifactChannel:
    """Pure request/response engine over a frozen list of named artifacts."""

    REQ_LBA = REQ_LBA
    REQ_BLOCKS = REQ_BLOCKS
    RESP_LBA = RESP_LBA
    WINDOW_BLOCKS = WINDOW_BLOCKS
    PAYLOAD_CAP = PAYLOAD_CAP
    SENTINEL = SENTINEL

    def __init__(self, token):
        if not isinstance(token, (bytes, bytearray)) or len(token) != 16:
            raise ValueError('session token must be 16 bytes')
        self.token = bytes(token)
        self._items = None          # tuple of (name, bytes, sha256 raw)
        self._req = None
        self._resp = None

    def register(self, artifacts):
        """Register the ordered artifact list exactly once. `artifacts` is a
        sequence of (name, bytes). Bytes are copied (frozen snapshot)."""
        if self._items is not None:
            raise RuntimeError('artifacts already registered; channel is single-shot')
        items = []
        names = set()
        for entry in artifacts:
            name, data = entry
            if not valid_name(name):
                raise ValueError(f'invalid artifact name {name!r}')
            if name.lower() in names:
                raise ValueError(f'duplicate artifact name {name!r}')
            if not isinstance(data, (bytes, bytearray)):
                raise TypeError('artifact must be bytes-like')
            names.add(name.lower())
            blob = bytes(data)
            items.append((name, blob, hashlib.sha256(blob).digest()))
        if not 1 <= len(items) <= MAX_ARTIFACTS:
            raise ValueError('need 1..64 artifacts')
        self._items = tuple(items)
        self._req = None
        self._resp = self._build_window(None)
        return self

    @property
    def count(self):
        return 0 if self._items is None else len(self._items)

    def describe(self):
        return [{'index': i, 'name': n, 'bytes': len(b), 'sha256': s.hex()}
                for i, (n, b, s) in enumerate(self._items or ())]

    @staticmethod
    def is_request_write(lba, n):
        return lba == REQ_LBA and n == REQ_BLOCKS

    @staticmethod
    def intersects_window(lba, n):
        return lba < RESP_LBA + WINDOW_BLOCKS and lba + n > RESP_LBA

    def _parse_request(self, data):
        if len(data) != BLOCK:
            return None
        if data[0:16] != REQ_MAGIC or data[16:32] != self.token:
            return None
        rid, offset, length, index, crc = struct.unpack_from('<IQIII', data, 32)
        if zlib.crc32(data[0:52]) != crc:
            return None
        if any(data[56:]):
            return None
        if rid < 1:
            return None
        return rid, index, offset, length

    def handle_request(self, data):
        """Validate and latch a request; True=SUCCESS, False=INVALID_FIELD.
        A rejected request leaves state unchanged."""
        if self._items is None:
            return False
        parsed = self._parse_request(data)
        if parsed is None:
            return False
        rid, index, offset, length = parsed
        if index >= len(self._items):
            return False
        size = len(self._items[index][1])
        if offset == SENTINEL:
            if length != 0:
                return False
            norm = ('manifest', rid, index, SENTINEL, 0)
        else:
            if not 1 <= length <= PAYLOAD_CAP or offset > size:
                return False
            norm = ('data', rid, index, offset, length)
        if self._req is not None and norm == self._req:
            return True
        last_id = 0 if self._req is None else self._req[1]
        if rid <= last_id:
            return False
        self._req = norm
        self._resp = self._build_window(norm)
        return True

    def _build_window(self, norm):
        if norm is None:
            status, rid, index, off, payload = STATUS_NONE, 0, 0, 0, b''
            size, sha = 0, bytes(32)
        else:
            kind, rid, index, off, length = norm
            name, blob, sha = self._items[index]
            size = len(blob)
            if kind == 'manifest':
                status, payload = STATUS_MANIFEST, name.encode('ascii')
            else:
                served = min(length, size - off)
                payload = blob[off:off + served]
                status = STATUS_DATA if served == length else STATUS_FINAL
        header = bytearray(HEADER)
        header[0:16] = RESP_MAGIC
        header[16:32] = self.token
        struct.pack_into('<IIQQII', header, 32, rid, status, size, off,
                         len(payload), zlib.crc32(payload))
        header[64:96] = sha
        struct.pack_into('<II', header, 96, index, len(self._items))
        struct.pack_into('<I', header, 124, zlib.crc32(bytes(header[0:124])))
        window = bytearray(WINDOW_BYTES)
        window[0:HEADER] = header
        window[HEADER:HEADER + len(payload)] = payload
        return bytes(window)

    def response_window(self):
        if self._resp is None:
            raise RuntimeError('no artifacts registered')
        return self._resp

    def overlay(self, lba, n, original):
        """Replace only the bytes of a read that intersect LBA 129..144."""
        if self._resp is None:
            raise RuntimeError('no artifacts registered')
        if n < 1 or len(original) != n * BLOCK:
            raise ValueError('original must be n*BLOCK bytes')
        out = bytearray(original)
        lo = max(lba, RESP_LBA)
        hi = min(lba + n, RESP_LBA + WINDOW_BLOCKS)
        if lo < hi:
            src = (lo - RESP_LBA) * BLOCK
            dst = (lo - lba) * BLOCK
            size = (hi - lo) * BLOCK
            out[dst:dst + size] = self._resp[src:src + size]
        return bytes(out)
