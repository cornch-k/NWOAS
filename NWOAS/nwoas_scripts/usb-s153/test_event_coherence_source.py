import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
EXC = (ROOT / "m1n1_windows/src/hv_exc.c").read_text()
VM = (ROOT / "m1n1_windows/src/hv_vm.c").read_text()
LAUNCHER = (ROOT / "nwoas_scripts/usb-s153-guest-test.sh").read_text()


class S153UsbCEventCoherenceSourceTests(unittest.TestCase):
    def test_device_event_ring_is_invalidated_before_observation(self):
        start = EXC.index("/* S153: the USB-C xHC writes its event ring")
        end = EXC.index("/* D79: one read-only healthy-controller snapshot", start)
        block = EXC[start:end]
        self.assertLess(
            block.index("nwoas_usbc_event_coherence(rt_base, false)"),
            block.index("nwoas_usbc_poll_descriptor()"),
        )
        self.assertIn("nwoas_cache_maint(rp,count*16,false)", VM)

    def test_irq_requires_xhci_global_interrupt_enable(self):
        self.assertIn("bool global_ie = !!(read32(op_base + 0x00) & BIT(2));", EXC)
        self.assertIn("if (!armed || !pending || !global_ie)", EXC)
        gated = "if (armed && pending && global_ie && hv_vgic3_spi_enabled(NWOAS_USB_SPI))"
        self.assertIn(gated, EXC)

    def test_event_data_and_immediate_payload_rules_are_unchanged(self):
        self.assertIn("(ctl&BIT(6))", VM)
        self.assertIn("(type!=1 && type!=3)", VM)
        self.assertIn("Event Data opaque cookies and immediate data are never addresses", VM)

    def test_dma_error_baseline_and_first_hse_are_logged(self):
        self.assertIn('nwoas_usbc_error_snapshot("spi-enable")', EXC)
        self.assertIn('nwoas_usbc_error_snapshot("first-hse")', EXC)
        self.assertIn("NWOAS_XHCI_BASE + 0xc130", EXC)
        self.assertIn("0x502f80000UL", EXC)

    def test_launcher_pins_s153_and_keeps_s152_stack(self):
        self.assertIn("m1n1-s153-usbc-event-coherence.bin", LAUNCHER)
        self.assertIn(
            "916e402e45a101563f861f925d14da95836ad188b71dd04a6c7051a9feaf3180",
            LAUNCHER,
        )
        self.assertIn("m1n1-payload-s135-8cpu-speed.bin", LAUNCHER)
        self.assertIn("pcie_emul-d81-usbc-payload.py", LAUNCHER)
        self.assertIn("nvme-s130/guest_module.py", LAUNCHER)


if __name__ == "__main__":
    unittest.main()
