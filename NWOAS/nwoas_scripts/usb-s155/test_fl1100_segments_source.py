import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
VM = (ROOT / "m1n1_windows/src/hv_vm.c").read_text()


class S155Fl1100SegmentSourceTests(unittest.TestCase):
    def setUp(self):
        start = VM.index("static void nwoas_fl_inval_evt")
        self.helper = VM[start : VM.index("#if NWOAS_COH_KEEP", start)]

    def test_uses_hardware_rtsoff_and_erstsz(self):
        self.assertIn("hv_pa_read(ctx, bar + 0x18, &rtsoff, 2)", self.helper)
        self.assertIn("bar + rtsoff + 0x28, &erstsz_raw, 2", self.helper)
        self.assertIn("segments = erstsz_raw & 0xffff", self.helper)

    def test_all_bounded_segments_are_walked(self):
        self.assertIn("segments > 16", self.helper)
        self.assertIn("for (u32 i = 0; i < segments; i++)", self.helper)
        self.assertIn("count > 4096", self.helper)
        self.assertIn("ring_len = (size_t)count * 16", self.helper)

    def test_table_is_cleaned_and_event_segments_are_invalidate_only(self):
        self.assertIn("nwoas_fl_cache_guest_range(erst, table_len, true)", self.helper)
        self.assertIn("nwoas_fl_cache_guest_range(ring, ring_len, false)", self.helper)
        self.assertNotIn("nwoas_fl_cache_guest_range(ring, ring_len, true)", self.helper)

    def test_usb_status_and_iman_pending_share_the_helper(self):
        gate = VM.index("(off == 0x84 && (val[0] & 0x8))")
        call = VM.index("nwoas_fl_inval_evt(ctx);", gate)
        block = VM[gate:call]
        self.assertIn("off == 0x2020 && (val[0] & 0x1)", block)
        self.assertIn("!nwoas_synth_pending", block)
        self.assertIn("(elr >> 48) == 0xffff", block)


if __name__ == "__main__":
    unittest.main()
