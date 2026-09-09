import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
VM = (ROOT / "m1n1_windows/src/hv_vm.c").read_text()
EXC = (ROOT / "m1n1_windows/src/hv_exc.c").read_text()
LAUNCHER = (ROOT / "nwoas_scripts/usb-s154-guest-test.sh").read_text()


class S154UsbCPollCoherenceSourceTests(unittest.TestCase):
    def test_poll_invalidate_is_bounded_to_kernel_usbc_pending_reads(self):
        start = VM.index("/* S154: USBHUB3 resets the root hub")
        end = VM.index('dprintf("HV: SPTE_MAP[R]', start)
        block = VM[start:end]
        self.assertIn("(elr >> 48) == 0xffff", block)
        self.assertIn("width == 2", block)
        self.assertIn("paddr >= 0x502280000UL", block)
        self.assertIn("paddr < 0x502290000UL", block)
        self.assertIn("paddr == op + 0x04 && (val[0] & BIT(3))", block)
        self.assertIn("paddr == rt + 0x20 && (val[0] & BIT(0))", block)
        self.assertIn("nwoas_usbc_event_coherence(rt, false)", block)

    def test_event_ring_uses_invalidate_only(self):
        helper = VM[
            VM.index("void nwoas_usbc_event_coherence") :
            VM.index("/* D81:", VM.index("void nwoas_usbc_event_coherence"))
        ]
        self.assertIn("nwoas_cache_maint(rp,count*16,false)", helper)
        self.assertNotIn("nwoas_cache_maint(rp,count*16,true)", helper)

    def test_irq_path_retains_s153_gates(self):
        self.assertIn("if (!armed || !pending || !global_ie)", EXC)
        self.assertIn("nwoas_usbc_event_coherence(rt_base, false)", EXC)

    def test_launcher_pins_s154_and_full_stable_stack(self):
        self.assertIn("m1n1-s154-usbc-poll-coherence.bin", LAUNCHER)
        self.assertIn(
            "16d222cfd4996e53d141dba3fcd12d7a907aca9b8b5e58c2f420f5867b21868f",
            LAUNCHER,
        )
        self.assertIn("m1n1-payload-s135-8cpu-speed.bin", LAUNCHER)
        self.assertIn("pcie_emul-d81-usbc-payload.py", LAUNCHER)
        self.assertIn("nvme-s130/guest_module.py", LAUNCHER)


if __name__ == "__main__":
    unittest.main()
