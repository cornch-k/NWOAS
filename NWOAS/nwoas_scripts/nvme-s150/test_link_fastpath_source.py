import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).parents[2]
M1N1 = ROOT / "m1n1_windows"
sys.path.insert(0, str(M1N1 / "proxyclient"))

from m1n1.hv.types import HV_EVENT, NwoasNvmeLinkRequest


class LinkFastPathSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hv_h = (M1N1 / "src/hv.h").read_text()
        cls.hv_c = (M1N1 / "src/hv.c").read_text()
        cls.vm = (M1N1 / "src/hv_vm.c").read_text()
        cls.hv_py = (M1N1 / "proxyclient/m1n1/hv/__init__.py").read_text()
        cls.types = (M1N1 / "proxyclient/m1n1/hv/types.py").read_text()
        cls.link = (ROOT / "nwoas_scripts/uefi-s125/link_module.py").read_text()

    def test_c_and_python_abi_match(self):
        self.assertEqual(NwoasNvmeLinkRequest.sizeof(), 96)
        self.assertEqual(int(HV_EVENT.NWOAS_NVME_LINK), 8)
        self.assertIn("HV_NWOAS_NVME_LINK_MAGIC   0x53313530U", self.hv_h)
        self.assertIn("sizeof(struct hv_nwoas_nvme_link_req) == 96", self.hv_h)
        self.assertIn('req.magic!=0x53313530', self.link)

    def test_event_is_registered_and_always_returns_to_target(self):
        self.assertIn("HV_NWOAS_NVME_LINK,", self.hv_h)
        self.assertIn("def handle_nwoas_nvme_link", self.hv_py)
        self.assertIn("HV_EVENT.NWOAS_NVME_LINK,", self.hv_py)
        self.assertIn("self.p.exit(EXC_RET.HANDLED)", self.hv_py)

    def test_link_loss_propagates_and_ctrl_c_is_deferred(self):
        body = self.hv_py[self.hv_py.index("def handle_nwoas_nvme_link"):]
        body = body[:body.index("    def skip(self):")]
        self.assertIn("previous_in_handler = self._in_handler", body)
        self.assertIn("self._in_handler = True", body)
        self.assertIn("except (UartTimeout, UartChecksumError):", body)
        self.assertIn("raise", body)
        self.assertIn("self._in_handler = previous_in_handler", body)
        self.assertIn("if not self._in_handler and self._sigint_pending:", body)
        self.assertEqual(body.count("self.p.exit(EXC_RET.HANDLED)"), 1)

    def test_target_retains_queue_ownership(self):
        self.assertIn("cmd->nsid == 2", self.vm)
        self.assertIn("nwoas_nvme_link_execute(ctx, cmd, completion_result)", self.vm)
        handler = self.link[self.link.index("def _fast_link_handle"):]
        self.assertNotIn("_controller.process", handler)
        self.assertNotIn("_controller.write", handler)
        self.assertNotIn("_controller.cq", handler)
        self.assertNotIn("_controller.sq", handler)

    def test_request_validates_sequence_and_queue_generations(self):
        for text in (
            "req.sequence != sequence",
            "req.sq_generation != sq_generation",
            "req.cq_generation != cq_generation",
            "req.response != HV_NWOAS_NVME_LINK_DONE",
        ):
            self.assertIn(text, self.vm)
        self.assertIn("req.lifecycle_epoch != lifecycle_epoch", self.vm)

    def test_inflight_guard_and_lifecycle_cancellation(self):
        self.assertIn("bool link_busy;", self.vm)
        self.assertIn("nwoas_nvme_fp.link_busy = true", self.vm)
        self.assertIn("nwoas_nvme_fp.link_busy = false", self.vm)
        self.assertIn("nwoas_nvme_fp.faulted || nwoas_nvme_fp.link_busy", self.vm)
        self.assertIn("nwoas_nvme_fp.lifecycle_epoch++", self.vm)
        self.assertIn("NWOAS_NVME_EXEC_CANCELLED", self.vm)
        self.assertIn("if (action == 4)", self.vm)
        self.assertIn("nwoas_nvme_fp.deferred = nwoas_nvme_fp.sq_head != nwoas_nvme_fp.sq_tail", self.vm)
        self.assertIn("lifecycle=p.nwoas_nvme_fastpath(4)", self.link)
        self.assertLess(self.link.index("lifecycle=p.nwoas_nvme_fastpath(4)"),
                        self.link.index("_controller.ns.io(command,_memory)"))

    def test_ns2_protocol_failure_does_not_fault_ns1(self):
        self.assertIn(
            "return cmd->opcode == 1 ? NWOAS_NVME_SC_WRITE_ERROR : NWOAS_NVME_SC_READ_ERROR;",
            self.vm,
        )
        self.assertIn("req.status > 0x7fff", self.vm)

    def test_all_namespace_flush_is_compatible(self):
        self.assertIn("cmd->opcode == 0 && cmd->nsid == 0xffffffff", self.vm)
        self.assertIn("return nvme_flush(1)", self.vm)

    def test_context_reaches_doorbell_and_tick_paths(self):
        self.assertIn("nwoas_nvme_fastpath_process(ctx, 1);", self.vm)
        self.assertIn("nwoas_nvme_fastpath_poll(ctx);", self.hv_c)
        self.assertIn("nwoas_nvme_fastpath_mmio(ctx, ipa, val, true, width)", self.vm)
        self.assertIn("nwoas_nvme_fastpath_mmio(ctx, ipa, val, false, width)", self.vm)

    def test_python_executes_ns2_data_and_validates_prps(self):
        self.assertIn("_controller.ns.io(command,_memory)", self.link)
        self.assertIn("spans=resolve(", self.link)
        self.assertIn("_memory.write(guest_addr", self.link)
        self.assertIn("struct.unpack_from('<I',req.command,4)[0]!=2", self.link)

    def test_completion_result_is_propagated(self):
        self.assertIn("*completion_result = req.result", self.vm)
        self.assertIn(".result = completion_result", self.vm)
        self.assertIn("req.result=result.result", self.link)

    def test_struct_roundtrip_preserves_command(self):
        command = bytes(range(64))
        raw = NwoasNvmeLinkRequest.build(dict(
            magic=0x53313530, version=1, size=96, sequence=17,
            sq_generation=3, cq_generation=4, response=0,
            status=2, result=0, lifecycle_epoch=9, command=command,
        ))
        parsed = NwoasNvmeLinkRequest.parse(raw)
        self.assertEqual(parsed.command, command)
        self.assertEqual(parsed.sequence, 17)
        self.assertEqual(parsed.lifecycle_epoch, 9)


if __name__ == "__main__":
    unittest.main()
