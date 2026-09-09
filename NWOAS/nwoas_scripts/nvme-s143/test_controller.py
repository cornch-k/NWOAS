import struct
import sys
import unittest
import importlib.util
from pathlib import Path


HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "nvme-s124"))
sys.path.insert(0, str(HERE))

from readonly_namespace import ReadOnlyNamespace

_spec = importlib.util.spec_from_file_location("_nwoas_nvme_s143_test_controller", HERE / "controller.py")
_module = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _module
_spec.loader.exec_module(_module)
Controller = _module.Controller


class Memory:
    def __init__(self):
        self.b = bytearray(0x40000)

    def contains(self, addr, size):
        return addr >= 0x1000 and size > 0 and addr + size <= len(self.b)

    def read(self, addr, size):
        if not self.contains(addr, size):
            raise ValueError("bad RAM read")
        return bytes(self.b[addr:addr + size])

    def write(self, addr, data):
        if not self.contains(addr, len(data)):
            raise ValueError("bad RAM write")
        self.b[addr:addr + len(data)] = data


class TestS143Controller(unittest.TestCase):
    def setUp(self):
        self.memory = Memory()
        self.irq = []
        self.c = Controller(
            ReadOnlyNamespace(61279344, lambda lba, n=1: bytes(n * 4096)),
            self.memory,
            self.irq.append,
        )
        self.c.pci_write(4, 6, 16)
        self.c.write(0x24, 3 | (3 << 16), 32)
        self.c.write(0x28, 0x1000, 64)
        self.c.write(0x30, 0x2000, 64)
        self.c.write(0x14, 1 | (6 << 16) | (4 << 20), 32)
        self.c.cq[1] = type(self.c.cq[0])(0x3000, 4)
        self.c.sq[1] = type(self.c.sq[0])(0x4000, 4, 1)

    def put_io(self, slot, cid):
        cmd = bytearray(64)
        struct.pack_into("<BBH", cmd, 0, 0, 0, cid)  # Flush needs no PRPs.
        self.memory.write(0x4000 + slot * 64, cmd)

    def test_masked_sq_tail_is_deferred_until_unmask(self):
        for slot in range(3):
            self.put_io(slot, slot + 1)
        self.c.write(0x0c, 1, 32)
        self.c.write(0x1008, 3, 32)
        self.assertEqual(self.c.sq[1].head, 0)
        self.assertEqual(self.c.cq[1].pending, 0)

        self.c.write(0x10, 1, 32)
        self.assertEqual(self.c.sq[1].head, 1)
        self.assertEqual(self.c.cq[1].pending, 1)
        self.assertTrue(self.irq[-1])

    def test_each_dpc_handoff_releases_only_one_command(self):
        for slot in range(3):
            self.put_io(slot, slot + 1)
        self.c.write(0x0c, 1, 32)
        self.c.write(0x1008, 3, 32)

        for expected in (1, 2, 3):
            self.c.write(0x10, 1, 32)
            self.assertEqual(self.c.sq[1].head, expected)
            self.assertEqual(self.c.cq[1].pending, 1)
            self.c.write(0x0c, 1, 32)
            self.c.write(0x100c, expected, 32)

        self.assertEqual(self.c.sq[1].head, self.c.sq[1].tail)

    def test_unmasked_submission_retains_s139_behavior(self):
        self.put_io(0, 7)
        self.c.write(0x1008, 1, 32)
        self.assertEqual(self.c.sq[1].head, 1)
        self.assertEqual(self.c.cq[1].pending, 1)


if __name__ == "__main__":
    unittest.main()
