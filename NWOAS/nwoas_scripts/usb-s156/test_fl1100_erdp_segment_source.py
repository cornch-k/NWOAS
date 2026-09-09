import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
VM = (ROOT / "m1n1_windows/src/hv_vm.c").read_text()


class S156Fl1100ErdpSegmentSourceTests(unittest.TestCase):
    def setUp(self):
        start = VM.index("static void nwoas_fl_inval_evt")
        self.helper = VM[start : VM.index("#if NWOAS_COH_KEEP", start)]

    def test_reads_hardware_ring_layout_and_erdp(self):
        self.assertIn("bar + rtsoff + 0x28, &erstsz_raw, 2", self.helper)
        self.assertIn("bar + rtsoff + 0x38, &erdp_raw, 3", self.helper)
        self.assertIn("erdp = erdp_raw & ~0xfUL", self.helper)

    def test_validates_every_bounded_segment(self):
        self.assertIn("segments > 16", self.helper)
        self.assertIn("for (u32 i = 0; i < segments; i++)", self.helper)
        self.assertIn("count > 4096", self.helper)
        self.assertIn("ring_end < ring", self.helper)

    def test_invalidates_only_erdp_containing_segment(self):
        self.assertIn("erdp >= ring && erdp < ring_end", self.helper)
        self.assertIn("selected_segment = i", self.helper)
        self.assertIn("nwoas_fl_cache_guest_range(selected_ring", self.helper)
        self.assertNotIn("nwoas_fl_cache_guest_range(ring, ring_len", self.helper)

    def test_preloads_descriptors_and_covers_ring_boundary(self):
        self.assertIn("u64 rings[16] = {0}", self.helper)
        self.assertIn("selected_end - erdp <= 64", self.helper)
        self.assertIn("(selected_segment + 1) % segments", self.helper)
        self.assertIn("nwoas_fl_cache_guest_range(rings[next]", self.helper)

    def test_table_is_cleaned_before_descriptor_reads(self):
        table_clean = self.helper.index(
            "nwoas_fl_cache_guest_range(erst, table_len, true)"
        )
        descriptor_read = self.helper.index("entry_pa = nwoas_ipa_to_pa")
        self.assertLess(table_clean, descriptor_read)

    def test_pending_usb_status_and_iman_use_the_helper(self):
        gate = VM.index("(off == 0x84 && (val[0] & 0x8))")
        call = VM.index("nwoas_fl_inval_evt(ctx);", gate)
        block = VM[gate:call]
        self.assertIn("off == 0x2020 && (val[0] & 0x1)", block)
        self.assertIn("!nwoas_synth_pending", block)
        self.assertIn("(elr >> 48) == 0xffff", block)


if __name__ == "__main__":
    unittest.main()
