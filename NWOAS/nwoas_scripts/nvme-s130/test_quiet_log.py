import unittest

from quiet_log import QuietLogSampler


class QuietLogSamplerTests(unittest.TestCase):
    def test_successful_commands_are_sampled(self):
        sampler = QuietLogSampler()
        emitted = [
            sampler.should_emit(f"CMD q=1 op=02 cid={i} status=0 bytes=4096")
            for i in range(1, 2049)
        ]
        self.assertEqual(sum(emitted), 17)
        self.assertTrue(emitted[-1])

    def test_failed_command_is_always_emitted(self):
        sampler = QuietLogSampler()
        for i in range(100):
            sampler.should_emit(f"CMD q=1 op=02 cid={i} status=0 bytes=4096")
        self.assertTrue(
            sampler.should_emit("CMD q=1 op=02 cid=101 status=2 bytes=0")
        )

    def test_register_writes_remain_visible_and_doorbells_are_sampled(self):
        sampler = QuietLogSampler()
        self.assertTrue(sampler.should_emit("MMIO W 14/32=1"))
        emitted = [
            sampler.should_emit(f"MMIO W 1008/32={i:x}")
            for i in range(1, 4097)
        ]
        self.assertEqual(sum(emitted), 17)
        self.assertTrue(emitted[-1])

    def test_per_completion_interrupt_masks_share_routine_sampling(self):
        sampler = QuietLogSampler()
        emitted = []
        for _ in range(1, 2049):
            emitted.append(sampler.should_emit("MMIO W c/32=1"))
            emitted.append(sampler.should_emit("MMIO W 10/32=1"))
        self.assertEqual(sum(emitted), 17)
        self.assertTrue(emitted[-1])

    def test_error_and_queue_state_are_always_visible(self):
        sampler = QuietLogSampler()
        self.assertTrue(sampler.should_emit("CFS invalid queue"))
        self.assertTrue(sampler.should_emit("EN admin SQ=1000/64 CQ=2000/64"))
        self.assertTrue(sampler.should_emit("CREATE SQ 1 depth=64 base=3000"))


if __name__ == "__main__":
    unittest.main()
