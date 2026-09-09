import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
VM = (ROOT / "m1n1_windows/src/hv_vm.c").read_text()


class RealEventInvalidateSourceTests(unittest.TestCase):
    def test_guest_authored_erst_is_preserved(self):
        body = VM[VM.index("static void nwoas_fl_inval_evt"):]
        body = body[:body.index("#if NWOAS_COH_KEEP")]
        self.assertIn("nwoas_fl_cache_guest_range(erst, table_len, true)", body)
        self.assertNotIn("nwoas_fl_cache_guest_range(erst, table_len, false)", body)

    def test_only_device_written_event_ring_is_invalidated(self):
        body = VM[VM.index("static void nwoas_fl_inval_evt"):]
        body = body[:body.index("#if NWOAS_COH_KEEP")]
        self.assertIn("nwoas_fl_cache_guest_range(ring, ring_len, false)", body)
        self.assertNotIn("nwoas_fl_cache_guest_range(ring, ring_len, true)", body)

    def test_real_eint_path_survives_compiled_synth_support(self):
        needle = "(off == 0x84 && (val[0] & 0x8))"
        self.assertIn(needle, VM)
        call = VM.index("nwoas_fl_inval_evt(ctx);", VM.index(needle))
        prefix = VM[max(0, VM.rfind("\n", 0, VM.index(needle)) - 256):call]
        self.assertNotIn("#if !NWOAS_SYNTH_PSCE", prefix)


if __name__ == "__main__":
    unittest.main()
