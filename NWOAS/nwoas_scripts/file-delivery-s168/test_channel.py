"""S168 channel tests: framing, strict validation, monotonic ids / idempotent
retries, header+payload CRC, deterministic partial/overlapping/cross-port reads,
source-mutation immutability, and capacity/boundary behaviour.

Pure logic only -- no hardware, no file loading, no live modules.
"""
import struct
import unittest
import zlib

import channel as C
from channel import (
    HostArtifactChannel, BLOCK, HEADER, PAYLOAD_CAP, WINDOW_BLOCKS, WINDOW_BYTES,
    REQ_LBA, PORT_LBA, RESP_LBA, SENTINEL, REQ_MAGIC, RESP_MAGIC,
    STATUS_DATA, STATUS_FINAL, STATUS_NONE, STATUS_MANIFEST,
)

TOKEN = bytes(range(16))
OTHER_TOKEN = bytes(range(16, 32))


def build_request(token, rid, offset, length, *, crc=None, tail=b''):
    """Assemble a 4096-byte request block. `crc`/`tail` overridable for negatives."""
    b = bytearray(BLOCK)
    b[0:16] = REQ_MAGIC
    b[16:32] = token
    struct.pack_into('<IQII', b, 32, rid, offset, length, 0)
    real = zlib.crc32(bytes(b[0:48]))
    struct.pack_into('<I', b, 48, real if crc is None else crc)
    if tail:
        b[52:52 + len(tail)] = tail
    return bytes(b)


def parse_header(window):
    magic = window[0:16]
    token = window[16:32]
    rid, status, file_size, offset, length, pcrc = struct.unpack_from('<IIQQII', window, 32)
    sha = window[64:96]
    reserved = window[96:124]
    hcrc = struct.unpack_from('<I', window, 124)[0]
    payload = window[HEADER:HEADER + length]
    return dict(magic=magic, token=token, id=rid, status=status, file_size=file_size,
                offset=offset, length=length, pcrc=pcrc, sha=sha, reserved=reserved,
                hcrc=hcrc, payload=payload)


def make(artifact, token=TOKEN):
    return HostArtifactChannel(token).register(artifact)


class Framing(unittest.TestCase):
    def test_magic_lengths_are_16(self):
        self.assertEqual(len(REQ_MAGIC), 16)
        self.assertEqual(len(RESP_MAGIC), 16)
        self.assertNotEqual(REQ_MAGIC, RESP_MAGIC)

    def test_geometry_constants(self):
        self.assertEqual(HEADER, 128)
        self.assertEqual(WINDOW_BLOCKS, 16)
        self.assertEqual(WINDOW_BYTES, 65536)
        self.assertEqual(PAYLOAD_CAP, 65536 - 128)
        # 129..144 inclusive is 16 blocks and never overlaps the LBA-128 mailbox.
        self.assertEqual(RESP_LBA, PORT_LBA + 1)
        self.assertLess(PORT_LBA, RESP_LBA)

    def test_token_must_be_16(self):
        with self.assertRaises(ValueError):
            HostArtifactChannel(b'short')


class Registration(unittest.TestCase):
    def test_single_registration_only(self):
        ch = make(b'abc')
        with self.assertRaises(RuntimeError):
            ch.register(b'def')

    def test_register_requires_bytes(self):
        with self.assertRaises(TypeError):
            HostArtifactChannel(TOKEN).register(12345)

    def test_source_mutation_does_not_leak(self):
        src = bytearray(b'ORIGINAL-CONTENT' * 8)
        ch = make(src)
        before = ch.response_window()
        digest = ch.sha256_hex
        src[:] = b'TAMPERED-CONTENT' * 8            # mutate caller buffer afterwards
        ch.handle_request(build_request(TOKEN, 1, 0, 32))
        w = ch.response_window()
        self.assertEqual(parse_header(w)['payload'], b'ORIGINAL-CONTENT' * 2)
        self.assertEqual(ch.sha256_hex, digest)
        self.assertEqual(before, before)             # sanity: still readable

    def test_operations_before_register(self):
        ch = HostArtifactChannel(TOKEN)
        self.assertFalse(ch.registered)
        self.assertFalse(ch.handle_request(build_request(TOKEN, 1, 0, 16)))
        with self.assertRaises(RuntimeError):
            ch.response_window()


class NoRequestWindow(unittest.TestCase):
    def test_initial_window_is_none_status(self):
        ch = make(b'x' * 100)
        h = parse_header(ch.response_window())
        self.assertEqual(h['magic'], RESP_MAGIC)
        self.assertEqual(h['status'], STATUS_NONE)
        self.assertEqual(h['id'], 0)
        self.assertEqual(h['length'], 0)
        self.assertEqual(h['file_size'], 100)


class ValidRequests(unittest.TestCase):
    def setUp(self):
        self.data = bytes((i * 7 + 3) & 0xff for i in range(200000))
        self.ch = make(self.data)

    def test_basic_data_request(self):
        self.assertTrue(self.ch.handle_request(build_request(TOKEN, 1, 1000, 4096)))
        h = parse_header(self.ch.response_window())
        self.assertEqual(h['status'], STATUS_DATA)
        self.assertEqual(h['id'], 1)
        self.assertEqual(h['offset'], 1000)
        self.assertEqual(h['length'], 4096)
        self.assertEqual(h['payload'], self.data[1000:1000 + 4096])
        self.assertEqual(h['file_size'], len(self.data))

    def test_header_and_payload_crc(self):
        self.ch.handle_request(build_request(TOKEN, 5, 0, PAYLOAD_CAP))
        w = self.ch.response_window()
        h = parse_header(w)
        self.assertEqual(h['pcrc'], zlib.crc32(h['payload']))
        self.assertEqual(h['hcrc'], zlib.crc32(bytes(w[0:124])))
        self.assertEqual(h['reserved'], bytes(28))
        # header_crc protects every metadata field: flipping any of them breaks it.
        for field_off in (32, 36, 40, 48, 56, 60, 64):
            broken = bytearray(w[:124])
            broken[field_off] ^= 0xff
            self.assertNotEqual(zlib.crc32(bytes(broken)), h['hcrc'])

    def test_max_payload_chunk(self):
        self.assertTrue(self.ch.handle_request(build_request(TOKEN, 1, 0, PAYLOAD_CAP)))
        h = parse_header(self.ch.response_window())
        self.assertEqual(h['length'], PAYLOAD_CAP)
        self.assertEqual(h['payload'], self.data[:PAYLOAD_CAP])

    def test_sha256_in_header(self):
        self.ch.handle_request(build_request(TOKEN, 1, 0, 16))
        h = parse_header(self.ch.response_window())
        self.assertEqual(h['sha'].hex(), self.ch.sha256_hex)


class ManifestRequest(unittest.TestCase):
    def test_manifest(self):
        ch = make(b'Z' * 12345)
        self.assertTrue(ch.handle_request(build_request(TOKEN, 1, SENTINEL, 0)))
        h = parse_header(ch.response_window())
        self.assertEqual(h['status'], STATUS_MANIFEST)
        self.assertEqual(h['offset'], SENTINEL)
        self.assertEqual(h['length'], 0)
        self.assertEqual(h['file_size'], 12345)
        self.assertEqual(h['sha'].hex(), ch.sha256_hex)

    def test_manifest_rejects_nonzero_length(self):
        ch = make(b'Z' * 100)
        self.assertFalse(ch.handle_request(build_request(TOKEN, 1, SENTINEL, 16)))
        self.assertEqual(parse_header(ch.response_window())['status'], STATUS_NONE)


class StrictValidation(unittest.TestCase):
    def setUp(self):
        self.ch = make(b'D' * 100000)

    def _rejected_leaves_state(self, req):
        self.ch.handle_request(build_request(TOKEN, 1, 0, 16))     # latch a good one
        snap = self.ch.response_window()
        self.assertFalse(self.ch.handle_request(req))
        self.assertEqual(self.ch.response_window(), snap)          # unchanged

    def test_bad_magic(self):
        r = bytearray(build_request(TOKEN, 2, 0, 16)); r[0] ^= 0xff
        self._rejected_leaves_state(bytes(r))

    def test_wrong_token(self):
        self._rejected_leaves_state(build_request(OTHER_TOKEN, 2, 0, 16))

    def test_bad_crc(self):
        self._rejected_leaves_state(build_request(TOKEN, 2, 0, 16, crc=0xdeadbeef))

    def test_nonzero_tail(self):
        self._rejected_leaves_state(build_request(TOKEN, 2, 0, 16, tail=b'\x01'))

    def test_wrong_length_block(self):
        self._rejected_leaves_state(build_request(TOKEN, 2, 0, 16)[:-1])

    def test_id_zero(self):
        self._rejected_leaves_state(build_request(TOKEN, 0, 0, 16))

    def test_length_over_cap_rejected_not_clamped(self):
        self._rejected_leaves_state(build_request(TOKEN, 2, 0, PAYLOAD_CAP + 1))

    def test_length_zero_rejected(self):
        self._rejected_leaves_state(build_request(TOKEN, 2, 0, 0))

    def test_offset_past_eof_rejected(self):
        self._rejected_leaves_state(build_request(TOKEN, 2, 100001, 16))


class MonotonicAndIdempotent(unittest.TestCase):
    def setUp(self):
        self.ch = make(bytes(range(256)) * 400)     # 102400 bytes

    def test_monotonic_required(self):
        self.assertTrue(self.ch.handle_request(build_request(TOKEN, 5, 0, 16)))
        self.assertFalse(self.ch.handle_request(build_request(TOKEN, 4, 0, 16)))   # older
        self.assertFalse(self.ch.handle_request(build_request(TOKEN, 5, 32, 16)))  # same id, diff fields
        self.assertTrue(self.ch.handle_request(build_request(TOKEN, 6, 32, 16)))   # newer

    def test_identical_retry_is_idempotent(self):
        req = build_request(TOKEN, 7, 4096, 64)
        self.assertTrue(self.ch.handle_request(req))
        first = self.ch.response_window()
        for _ in range(4):
            self.assertTrue(self.ch.handle_request(req))          # replay same block
            self.assertEqual(self.ch.response_window(), first)    # byte-identical

    def test_stale_replay_after_advance_rejected(self):
        old = build_request(TOKEN, 1, 0, 16)
        self.assertTrue(self.ch.handle_request(old))
        self.assertTrue(self.ch.handle_request(build_request(TOKEN, 2, 16, 16)))
        after = self.ch.response_window()
        self.assertFalse(self.ch.handle_request(old))             # replayed stale id 1
        self.assertEqual(self.ch.response_window(), after)


class CapacityAndBoundaries(unittest.TestCase):
    def setUp(self):
        self.size = 3 * PAYLOAD_CAP + 100
        self.data = bytes((i * 131 + 7) & 0xff for i in range(self.size))
        self.ch = make(self.data)

    def test_final_short_chunk(self):
        off = 3 * PAYLOAD_CAP
        self.assertTrue(self.ch.handle_request(build_request(TOKEN, 1, off, PAYLOAD_CAP)))
        h = parse_header(self.ch.response_window())
        self.assertEqual(h['status'], STATUS_FINAL)               # fewer bytes remained
        self.assertEqual(h['length'], 100)
        self.assertEqual(h['payload'], self.data[off:])

    def test_offset_exactly_eof_is_empty_final(self):
        self.assertTrue(self.ch.handle_request(build_request(TOKEN, 1, self.size, 16)))
        h = parse_header(self.ch.response_window())
        self.assertEqual(h['status'], STATUS_FINAL)
        self.assertEqual(h['length'], 0)
        self.assertEqual(h['payload'], b'')

    def test_full_sequential_reassembly(self):
        rid = 0
        chunks = []
        off = 0
        while off < self.size:
            rid += 1
            self.assertTrue(self.ch.handle_request(build_request(TOKEN, rid, off, PAYLOAD_CAP)))
            h = parse_header(self.ch.response_window())
            self.assertEqual(h['offset'], off)
            self.assertEqual(h['pcrc'], zlib.crc32(h['payload']))
            chunks.append(h['payload'])
            off += h['length']
            if h['status'] == STATUS_FINAL:
                break
        self.assertEqual(b''.join(chunks), self.data)


class OverlayReads(unittest.TestCase):
    def setUp(self):
        self.ch = make(bytes((i * 3 + 1) & 0xff for i in range(500000)))
        self.ch.handle_request(build_request(TOKEN, 1, 12345, PAYLOAD_CAP))
        self.win = self.ch.response_window()

    def slice_original(self, lba, n):
        # a recognizable non-window filler pattern for each block
        return bytes(((lba + i // BLOCK) * 17 + 5) & 0xff for i in range(n * BLOCK))

    def test_full_window_read(self):
        orig = self.slice_original(RESP_LBA, WINDOW_BLOCKS)
        got = self.ch.overlay(RESP_LBA, WINDOW_BLOCKS, orig)
        self.assertEqual(got, self.win)

    def test_partial_and_overlapping_are_deterministic(self):
        orig = self.slice_original(RESP_LBA, WINDOW_BLOCKS)
        full = self.ch.overlay(RESP_LBA, WINDOW_BLOCKS, orig)
        # every sub-range must equal the matching slice of the full window,
        # and must be identical when re-read (immutable latched bytes).
        for start in range(WINDOW_BLOCKS):
            for count in range(1, WINDOW_BLOCKS - start + 1):
                sub = self.slice_original(RESP_LBA + start, count)
                a = self.ch.overlay(RESP_LBA + start, count, sub)
                b = self.ch.overlay(RESP_LBA + start, count, sub)
                self.assertEqual(a, b)
                exp = full[start * BLOCK:(start + count) * BLOCK]
                self.assertEqual(a, exp)

    def test_reads_outside_window_untouched(self):
        for lba in (0, 100, REQ_LBA, PORT_LBA, RESP_LBA + WINDOW_BLOCKS, 8000):
            orig = self.slice_original(lba, 1)
            self.assertEqual(self.ch.overlay(lba, 1, orig), orig)

    def test_cross_port_read_preserves_mailbox_and_request_blocks(self):
        # read spanning 127,128,129,130,131: only 129..131 come from the window.
        lba, n = REQ_LBA, 5
        orig = self.slice_original(lba, n)
        got = self.ch.overlay(lba, n, orig)
        # LBA 127 (request) and 128 (existing mailbox) are preserved verbatim.
        self.assertEqual(got[0:BLOCK], orig[0:BLOCK])
        self.assertEqual(got[BLOCK:2 * BLOCK], orig[BLOCK:2 * BLOCK])
        # 129,130,131 == first three window blocks.
        self.assertEqual(got[2 * BLOCK:5 * BLOCK], self.win[0:3 * BLOCK])

    def test_tail_crossing_window_end(self):
        # read 143,144,145,146: 143,144 in window; 145,146 preserved.
        lba, n = 143, 4
        orig = self.slice_original(lba, n)
        got = self.ch.overlay(lba, n, orig)
        self.assertEqual(got[0:2 * BLOCK], self.win[(143 - RESP_LBA) * BLOCK:])
        self.assertEqual(got[2 * BLOCK:], orig[2 * BLOCK:])

    def test_overlay_length_validation(self):
        with self.assertRaises(ValueError):
            self.ch.overlay(RESP_LBA, 2, b'\x00' * BLOCK)      # wrong length
        with self.assertRaises(ValueError):
            self.ch.overlay(RESP_LBA, 0, b'')


class AdapterHelpers(unittest.TestCase):
    def test_is_request_write(self):
        self.assertTrue(HostArtifactChannel.is_request_write(REQ_LBA, 1))
        self.assertFalse(HostArtifactChannel.is_request_write(REQ_LBA, 2))
        self.assertFalse(HostArtifactChannel.is_request_write(PORT_LBA, 1))

    def test_intersects_window(self):
        self.assertTrue(HostArtifactChannel.intersects_window(RESP_LBA, 1))
        self.assertTrue(HostArtifactChannel.intersects_window(127, 5))
        self.assertTrue(HostArtifactChannel.intersects_window(144, 1))
        self.assertFalse(HostArtifactChannel.intersects_window(PORT_LBA, 1))
        self.assertFalse(HostArtifactChannel.intersects_window(145, 4))


if __name__ == '__main__':
    unittest.main(verbosity=2)
