import hashlib
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
M1N1 = ROOT / "m1n1_windows"
LAUNCHER = (ROOT / "nwoas_scripts/usb-s152-guest-test.sh").read_text()
VM = (M1N1 / "src/hv_vm.c").read_text()
USBC = M1N1 / "pcie_emul-d81-usbc-payload.py"
PAYLOAD = M1N1 / "m1n1-payload-s135-8cpu-speed.bin"


class S152CandidateSourceTests(unittest.TestCase):
    def test_candidate_artifacts_are_exact(self):
        self.assertEqual(PAYLOAD.stat().st_size, 32342016)
        self.assertEqual(
            hashlib.sha256(PAYLOAD.read_bytes()).hexdigest(),
            "73f9623d6cf8060c387e2a87119391ed66a49f93ec70b4cd628dc40a8df93e11",
        )
        self.assertEqual(
            hashlib.sha256(USBC.read_bytes()).hexdigest(),
            "ee1027d1c868114fb09bafbdb19b50b312b2c3fc456ee3f9309ccbcfa9f07f64",
        )

    def test_launcher_uses_visible_xhc1_payload_and_d83_module(self):
        self.assertIn("m1n1-payload-s135-8cpu-speed.bin", LAUNCHER)
        self.assertIn("pcie_emul-d81-usbc-payload.py", LAUNCHER)
        self.assertNotIn("pcie_usba_only.py", LAUNCHER)
        self.assertIn("NWOAS_WIN_SKEW=0x34000", LAUNCHER)

    def test_d83_runtime_path_is_present(self):
        module = USBC.read_text()
        self.assertIn("0x502280000, 0x10000", module)
        self.assertIn("nwoas_usbc_payload_alias", VM)
        self.assertIn("live_segment[ep]", VM)
        self.assertIn("paddr >= 0x502280000UL", VM)


if __name__ == "__main__":
    unittest.main()
