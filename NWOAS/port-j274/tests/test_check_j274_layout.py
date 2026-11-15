"""Offline tests for the J274 draft layout. Run: python3 -I -m unittest discover -s tests

These tests only exercise arithmetic and file contents. They read the reference
J313 layout JSON as plain data (json.loads); no reference code is imported or run.
Passing them says nothing about booting on hardware.
"""
import json
import sys
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PORT / "tools"))
import check_j274_layout as c  # noqa: E402

REF_J313_LAYOUT = PORT.parent / "external/windows-on-m1/config/j313-guest-layout.json"
J274_LAYOUT = PORT / "config/j274-guest-layout.json"


def load(path):
    return c.decode(json.loads(path.read_text()))


class KeySet(unittest.TestCase):
    def test_same_keys_as_reference_file(self):
        ref_keys = set(json.loads(REF_J313_LAYOUT.read_text()).keys())
        ours = set(json.loads(J274_LAYOUT.read_text()).keys())
        self.assertEqual(ref_keys, set(c.FIELDS))
        self.assertEqual(ours, set(c.FIELDS))


class RulesMatchReference(unittest.TestCase):
    def test_reference_j313_file_passes_restated_rules(self):
        # Sanity check of the re-statement: the reference's own file must pass.
        self.assertEqual(c.reference_rule_violations(load(REF_J313_LAYOUT)), [])


class ArithmeticMatchesOurLogs(unittest.TestCase):
    """S231 boot nwoas_scripts/logs/layout-s231-20261115-191038.Vc0lr2:320-337."""

    def test_s231_guest_base_and_bootargs(self):
        lay = c.assisted_layout(0x83ca24000, 0x5c000, 0x60000, 0x201c000, 0x59c000, 0x40000)
        self.assertEqual(lay["firmware_base"], 0x83dae0000)   # "Guest region start"
        self.assertEqual(lay["boot_args_base"], 0x8400d8000)  # "Setting up bootargs at"

    def test_s231_low_window_backing(self):
        # guest_module.py:44 / guest_map.py:14 with mem_top 0xbe1000000 (log :320, :348)
        self.assertEqual(c.J274_PLATFORM_RAM_END - 0x100000000 - 0x34000, 0xae0fcc000)
        self.assertEqual(load(J274_LAYOUT)["low_mem_pa"], 0xae0fcc000)


class J274Draft(unittest.TestCase):
    def setUp(self):
        self.lay = load(J274_LAYOUT)

    def test_pinned_addresses_derive_exactly(self):
        notes = c.j274_extra_findings(self.lay, c.J274_FW_SIZE_PADDED)
        self.assertFalse([n for n in notes if n.startswith("MISMATCH")], notes)

    def test_unpadded_fd_would_move_boot_args(self):
        notes = c.j274_extra_findings(self.lay, c.J274_FW_SIZE_UNPADDED)
        self.assertTrue(any(n.startswith("MISMATCH boot_args_base") for n in notes), notes)

    def test_only_known_reference_rule_deviation(self):
        bad = c.reference_rule_violations(self.lay)
        self.assertEqual(len(bad), 1, bad)
        self.assertTrue(bad[0].startswith("low_mem_ipa < 0x1000"))

    def test_low_window_size_does_not_fit_uint32_pcd(self):
        notes = c.j274_extra_findings(self.lay, c.J274_FW_SIZE_PADDED)
        self.assertTrue(any("UINT32" in n for n in notes), notes)

    def test_ecam_avoids_real_apcie(self):
        notes = c.j274_extra_findings(self.lay, c.J274_FW_SIZE_PADDED)
        self.assertFalse(any(n.startswith("CONFLICT") for n in notes), notes)

    def test_reference_ecam_would_conflict_on_j274(self):
        lay = dict(self.lay, pci_ecam=0x690000000)
        notes = c.j274_extra_findings(lay, c.J274_FW_SIZE_PADDED)
        self.assertTrue(any(n.startswith("CONFLICT") for n in notes), notes)

    def test_phys_base_above_every_observed_natural_base(self):
        # Highest natural guest base seen in nwoas_scripts/logs ("Physical memory:" lines).
        self.assertGreater(self.lay["phys_base"], 0x83dbdc000)
        self.assertEqual(self.lay["ram_end"], c.J274_PLATFORM_RAM_END)


if __name__ == "__main__":
    unittest.main()
