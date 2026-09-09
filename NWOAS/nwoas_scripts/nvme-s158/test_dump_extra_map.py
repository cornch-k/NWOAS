"""Regression checks against a captured ARM64 triage dump and truncated input."""
import struct
import tempfile
import unittest
from pathlib import Path
from dump_extra_map import Dump, hexdump

SAMPLE = Path(__file__).resolve().parents[1] / "logs/S154-050722-6296-01.dmp"

class ExtraMapTest(unittest.TestCase):
    def test_real_dump_finds_watchdog_block_and_kdbg(self):
        d = Dump(SAMPLE)
        d.parse(); d.parse_modules(); d.parse_regions()
        self.assertEqual(d.kdbg, (b"KDBG", 0xfffff80100400000))
        hits = d.lookup(d.params[3])
        self.assertEqual(len(hits), 1)
        self.assertEqual(d.u32(hits[0][2]), 0xaebecede)
        self.assertFalse(any("ValidOffset" in x for x in d.notes))
        # Profile start from the public watchdog triage header is not mapped.
        off = struct.unpack_from("<H", d.blob, hits[0][2] + 8)[0]
        self.assertEqual(d.lookup(d.params[3] + off), [])

    def test_truncated_table_never_claims_watchdog_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "truncated.dmp"
            p.write_bytes(SAMPLE.read_bytes()[:0x14c00])
            d = Dump(p); d.parse(); d.parse_modules(); d.parse_regions()
            self.assertEqual(d.lookup(d.params[3]), [])
            self.assertTrue(any("DataBlocks table" in x for x in d.notes))

    def test_partial_hexdump_does_not_leak_past_requested_end(self):
        self.assertNotIn("53", hexdump(b"ABS", 0, 2, 0))

    def test_cumulative_watchdog_uses_arg3_not_zero_arg4(self):
        d = Dump(SAMPLE); d.parse(); d.parse_modules(); d.parse_regions()
        target = d.params[3]
        d.params = [1, 0x1e00, target, 0]
        arg, pointer = d.watchdog_pointer()
        self.assertEqual(arg, 3)
        self.assertEqual(d.u32(d.lookup(pointer)[0][2]), 0xaebecede)

if __name__ == "__main__":
    unittest.main()
