"""S168: isolated host artifact delivery channel (pure logic, no I/O).

This module is deliberately self-contained. It performs NO hardware access, NO
file loading, NO module reload, and holds NO reference to any live namespace,
physical backend, or MMIO. It is a pure state machine that the main agent wraps
with a `LinkNamespace` subclass adapter (see README, "Adapter integration").

The channel exposes one immutable, explicitly registered `bytes` artifact over a
request/response mailbox that lives entirely inside the pre-FAT gap of the NS2
host-RAM image built by transport-s123:

    LBA 127  request port   guest WRITE, exactly 1 block (4096 B)
    LBA 128  existing job/stdout mailbox   -- NOT touched by this channel
    LBA 129..144  response window   guest READ, up to 16 blocks (64 KiB)

Design invariants (mirroring the S168 review in ../claude-s156):
  * A response is a PURE function of the latched request plus the frozen
    artifact. Repeated / partial / overlapping reads are byte-identical.
  * A request is validated STRICTLY. Malformed offsets/lengths are rejected
    (INVALID), never silently clamped. A legitimate short final chunk near EOF
    is a defined "final-short" outcome, not a rejection.
  * Request ids are strictly monotonic; an identical (id, offset, length) retry
    is idempotent. Any rejected request leaves channel state unchanged.
  * The guest never supplies a host path. The single artifact is registered
    once by the host and frozen (its bytes are copied on registration).
  * LBA 128, the FAT partition, the NS capacity and NS1 are never referenced
    here; `overlay()` only rewrites bytes inside 129..144.
"""
import hashlib
import struct
import zlib

BLOCK = 4096

# --- mailbox geometry (all inside the transport-s123 pre-FAT gap) -----------
REQ_LBA = 127          # request port; guest writes exactly REQ_BLOCKS block(s)
REQ_BLOCKS = 1
PORT_LBA = 128         # existing job/stdout mailbox -- left strictly alone
RESP_LBA = 129         # first block of the response window
WINDOW_BLOCKS = 16     # 129..144 inclusive == 16 blocks == 64 KiB == mdts=4 ceiling

# --- wire framing -----------------------------------------------------------
REQ_MAGIC = b'NWOAS-S16x-REQ !'
RESP_MAGIC = b'NWOAS-S16x-FILE!'
# The spec calls for explicit 16-byte magics; verify the literals really are 16.
assert len(REQ_MAGIC) == 16, len(REQ_MAGIC)
assert len(RESP_MAGIC) == 16, len(RESP_MAGIC)

HEADER = 128                                   # response header, payload follows
WINDOW_BYTES = WINDOW_BLOCKS * BLOCK           # 65536
PAYLOAD_CAP = WINDOW_BYTES - HEADER            # 65408 bytes of artifact per chunk
assert WINDOW_BYTES == 65536 and PAYLOAD_CAP == 65408

SENTINEL = 0xFFFFFFFFFFFFFFFF                   # request offset meaning "manifest"

# response status codes
STATUS_DATA = 0        # full requested length served
STATUS_FINAL = 1       # final-short / EOF: fewer bytes than requested remained
STATUS_NONE = 2        # no valid request latched yet
STATUS_MANIFEST = 3    # manifest response (payload empty; size+sha in header)


class HostArtifactChannel:
    """Pure request/response engine for a single immutable host artifact.

    Lifecycle:
        ch = HostArtifactChannel(token16)   # bind to the boot session token
        ch.register(artifact_bytes)         # exactly once; bytes are copied
        ok = ch.handle_request(block4096)   # latch a request (adapter: LBA 127)
        img = ch.overlay(lba, n, original)  # overlay the window into a read

    No method mutates the artifact, and `overlay` never rewrites anything outside
    the 129..144 window, so LBA 128 / FAT / capacity / NS1 are preserved.
    """

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
        self._artifact = None      # frozen bytes, set exactly once by register()
        self.file_size = None
        self.sha256 = None         # 32 raw bytes
        self._req = None           # latched normalized request tuple, or None
        self._resp = None          # cached immutable response window (bytes)

    # -- registration --------------------------------------------------------
    def register(self, artifact):
        """Register the one and only artifact. Idempotent-hostile: a second call
        raises. The bytes are copied so later mutation of the caller's buffer
        cannot change what the channel serves."""
        if self._artifact is not None:
            raise RuntimeError('artifact already registered; channel is single-shot')
        if not isinstance(artifact, (bytes, bytearray)):
            raise TypeError('artifact must be bytes-like')
        self._artifact = bytes(artifact)               # copy -> immutable snapshot
        self.file_size = len(self._artifact)
        self.sha256 = hashlib.sha256(self._artifact).digest()
        self._req = None
        self._resp = self._build_window(None)          # initial "no request" window
        return self

    @property
    def registered(self):
        return self._artifact is not None

    @property
    def sha256_hex(self):
        return None if self.sha256 is None else self.sha256.hex()

    # -- adapter helpers (make the LinkNamespace subclass trivial) ------------
    @staticmethod
    def is_request_write(lba, n):
        """True iff a write of n blocks at `lba` is the request port write."""
        return lba == REQ_LBA and n == REQ_BLOCKS

    @staticmethod
    def intersects_window(lba, n):
        """True iff a read of n blocks at `lba` touches the response window."""
        return lba < RESP_LBA + WINDOW_BLOCKS and lba + n > RESP_LBA

    # -- request path --------------------------------------------------------
    def _parse_request(self, data):
        """Structural validation of a 4096-byte request block. Returns
        (kind, id, offset, length) or None. Never mutates state."""
        if len(data) != BLOCK:
            return None
        if data[0:16] != REQ_MAGIC or data[16:32] != self.token:
            return None
        rid, offset, length, crc = struct.unpack_from('<IQII', data, 32)
        if zlib.crc32(data[0:48]) != crc:
            return None
        if any(data[52:]):                             # zero tail required
            return None
        if rid < 1:                                    # ids are 1-based
            return None
        kind = 'manifest' if offset == SENTINEL else 'data'
        return kind, rid, offset, length

    def handle_request(self, data):
        """Validate and latch a request. Returns True on accept (SUCCESS), False
        on any rejection (INVALID_FIELD). On False, channel state is unchanged.

        The adapter should map True->SUCCESS and False->INVALID_FIELD, and must
        only call this for a single-block write to REQ_LBA."""
        if self._artifact is None:
            return False
        parsed = self._parse_request(data)
        if parsed is None:
            return False
        kind, rid, offset, length = parsed

        # strict semantic validation -- reject, do not clamp, malformed requests
        if kind == 'manifest':
            if length != 0:
                return False
            norm = ('manifest', rid, SENTINEL, 0)
        else:
            if not (1 <= length <= PAYLOAD_CAP):
                return False
            if offset > self.file_size:                # == file_size is a valid EOF probe
                return False
            norm = ('data', rid, offset, length)

        # idempotent retry: the exact same latched request re-arrives.
        if self._req is not None and norm == self._req:
            return True
        # otherwise the id must be strictly greater than the last accepted id.
        last_id = 0 if self._req is None else self._req[1]
        if rid <= last_id:
            return False

        self._req = norm
        self._resp = self._build_window(norm)
        return True

    # -- response path -------------------------------------------------------
    def _build_window(self, norm):
        """Build the full immutable 64 KiB response window for a latched request
        tuple (or None). Pure function of (norm, frozen artifact, token)."""
        if norm is None:
            status, rid, off, served, payload = STATUS_NONE, 0, 0, 0, b''
        elif norm[0] == 'manifest':
            status, rid, off, served, payload = STATUS_MANIFEST, norm[1], SENTINEL, 0, b''
        else:
            _, rid, off, length = norm
            avail = self.file_size - off
            served = min(length, avail)
            payload = self._artifact[off:off + served]
            status = STATUS_DATA if served == length else STATUS_FINAL

        header = bytearray(HEADER)
        header[0:16] = RESP_MAGIC
        header[16:32] = self.token
        struct.pack_into('<I', header, 32, rid)
        struct.pack_into('<I', header, 36, status)
        struct.pack_into('<Q', header, 40, self.file_size)
        struct.pack_into('<Q', header, 48, off)
        struct.pack_into('<I', header, 56, served)
        struct.pack_into('<I', header, 60, zlib.crc32(payload))
        header[64:96] = self.sha256
        # bytes 96:124 stay zero (reserved). header_crc protects ALL metadata.
        struct.pack_into('<I', header, 124, zlib.crc32(bytes(header[0:124])))

        window = bytearray(WINDOW_BYTES)
        window[0:HEADER] = header
        window[HEADER:HEADER + served] = payload
        return bytes(window)

    def response_window(self):
        """The full immutable 16-block response window for the current latched
        request. Byte-identical across calls until a new request is latched."""
        if self._resp is None:
            raise RuntimeError('no artifact registered')
        return self._resp

    def overlay(self, lba, n, original):
        """Return the bytes for a read of `n` blocks starting at `lba`.

        Only the portion intersecting the 129..144 response window is replaced
        with window bytes; every other byte of `original` is preserved verbatim.
        `original` must be exactly n*BLOCK bytes (the host-RAM image slice)."""
        if self._resp is None:
            raise RuntimeError('no artifact registered')
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
