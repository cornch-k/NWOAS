import re
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
M1N1 = ROOT / "m1n1_windows"


class FastPathSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vm = (M1N1 / "src/hv_vm.c").read_text()
        cls.exc = (M1N1 / "src/hv_exc.c").read_text()
        cls.proxy_h = (M1N1 / "src/proxy.h").read_text()
        cls.proxy_py = (M1N1 / "proxyclient/m1n1/proxy.py").read_text()
        cls.guest = (ROOT / "nwoas_scripts/nvme-s130/guest_module.py").read_text()

    def test_proxy_abi_matches(self):
        self.assertRegex(
            self.proxy_h,
            r"P_HV_NWOAS_NVME_IRQ\s*=\s*0xc30,\s*P_HV_NWOAS_NVME_FASTPATH,",
        )
        self.assertIn("P_HV_NWOAS_NVME_FASTPATH = 0xc31", self.proxy_py)
        self.assertIn("case P_HV_NWOAS_NVME_FASTPATH", (M1N1 / "src/proxy.c").read_text())

    def test_admin_and_fast_interrupts_are_independent(self):
        self.assertIn("nwoas_nvme_irq_host_level", self.exc)
        self.assertIn("nwoas_nvme_irq_fast_level", self.exc)
        self.assertIn(
            "nwoas_nvme_irq_host_level || nwoas_nvme_irq_fast_level", self.exc
        )
        self.assertIn("nwoas_nvme_set_fast_irq(level)", self.vm)

    def test_only_sq_doorbell_starts_io(self):
        body = re.search(
            r"static bool nwoas_nvme_fastpath_mmio\(.*?\n}\n", self.vm, re.S
        ).group(0)
        self.assertEqual(body.count("nwoas_nvme_fastpath_process(ctx, 1);"), 1)
        sq = body.index("off == 0x1008")
        cq = body.index("off == 0x100c")
        process = body.index("nwoas_nvme_fastpath_process(ctx, 1);")
        self.assertLess(sq, process)
        self.assertLess(process, cq)

    def test_cq_ack_defers_backlog_to_hypervisor_tick(self):
        self.assertIn("nwoas_nvme_fp.deferred = nwoas_nvme_fp.sq_head != nwoas_nvme_fp.sq_tail", self.vm)
        self.assertIn("void nwoas_nvme_fastpath_poll(struct exc_info *ctx)", self.vm)
        hv = (M1N1 / "src/hv.c").read_text()
        self.assertIn("nwoas_nvme_fastpath_poll(ctx);", hv)

    def test_fatal_physical_failure_is_latched(self):
        self.assertIn("nwoas_nvme_fp.faulted = true", self.vm)
        self.assertIn("CSTS.RDY | CSTS.CFS", self.vm)

    def test_cqe_phase_is_published_last(self):
        self.assertIn("sizeof(cqe) - sizeof(cqe.status)", self.vm)
        self.assertIn("volatile u16", self.vm)

    def test_queue_generations_and_ien_are_forwarded(self):
        self.assertIn("fast_sq_generation=fast_cq_generation=0", self.guest)
        self.assertIn("c.cq[1].ien", self.guest)
        self.assertIn("old_sq is not new_sq", self.guest)

    def test_guest_prp1_offset_is_supported(self):
        nvme = (M1N1 / "src/nvme.c").read_text()
        self.assertIn("nvme_guest_pages[NVME_MAX_BLOCKS + 1]", nvme)
        self.assertIn("prp1 & (SZ_4K - 1)", nvme)

    def test_write_window_is_enforced_in_target(self):
        self.assertIn("#define NWOAS_NVME_WRITE_FIRST_LBA 53839104UL", self.vm)
        self.assertIn("#define NWOAS_NVME_WRITE_LAST_LBA  59968629UL", self.vm)
        self.assertIn("return NWOAS_NVME_SC_WRITE_RO;", self.vm)

    def test_guest_lifecycle_arms_and_disarms(self):
        self.assertIn("def _fast_arm_if_ready():", self.guest)
        self.assertIn("if fast_armed:p.nwoas_nvme_fastpath(0)", self.guest)
        self.assertIn("if not shutting_down:_fast_arm_if_ready()", self.guest)
        self.assertIn("if fast_armed and (sq_changed or cq_changed or not _fast_ready()):_fast_disable()", self.guest)
        self.assertIn("and not((c.cc>>14)&3)", self.guest)


if __name__ == "__main__":
    unittest.main()
