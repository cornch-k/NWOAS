import struct
import sys
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "nvme-s124"))
sys.path.insert(0, str(HERE))

from controller import Controller
from readonly_namespace import ReadOnlyNamespace


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


class TestS139Controller(unittest.TestCase):
    def setUp(self):
        self.memory = Memory()
        self.irq = []
        self.controller = Controller(
            ReadOnlyNamespace(61279344, lambda lba, n=1: bytes(n * 4096)),
            self.memory,
            self.irq.append,
        )
        self.controller.pci_write(4, 6, 16)
        self.controller.write(0x24, 3 | (3 << 16), 32)
        self.controller.write(0x28, 0x1000, 64)
        self.controller.write(0x30, 0x2000, 64)
        self.controller.write(0x14, 1 | (6 << 16) | (4 << 20), 32)

    def put_admin(self, slot, opcode=10, cid=1, dw10=7):
        cmd = bytearray(64)
        struct.pack_into("<BBH", cmd, 0, opcode, 0, cid)
        struct.pack_into("<I", cmd, 40, dw10)
        self.memory.write(self.controller.sq[0].base + slot * 64, cmd)

    def test_cq_ack_never_executes_sq_backlog(self):
        # Fill three of four CQ entries, then publish one additional command
        # without ringing its SQ doorbell yet.
        for slot in range(3):
            self.put_admin(slot, cid=slot)
        self.controller.write(0x1000, 3, 32)
        cq = self.controller.cq[0]
        sq = self.controller.sq[0]
        self.assertEqual(cq.pending, 3)
        self.put_admin(3, cid=3)
        sq.tail = 0

        # StorPort's CQ acknowledgement runs inside the completion DPC.  It
        # must only retire CQEs, even when an SQ has backlog.
        self.controller.write(0x1004, cq.tail, 32)
        self.assertEqual(cq.pending, 0)
        self.assertEqual(sq.head, 3)
        self.assertEqual(sq.tail, 0)

        # The next SQ-tail doorbell is the permitted execution boundary.
        self.controller.write(0x1000, 0, 32)
        self.assertEqual(sq.head, 0)
        self.assertEqual(cq.pending, 1)

    def test_invalid_cq_advance_still_sets_fatal(self):
        self.controller.write(0x1004, 1, 32)
        self.assertEqual(self.controller.csts & 2, 2)

    def test_advertises_one_io_queue_pair(self):
        cmd = bytearray(64)
        struct.pack_into("<BBH", cmd, 0, 10, 0, 9)
        struct.pack_into("<I", cmd, 40, 7)
        self.memory.write(self.controller.sq[0].base, cmd)
        self.controller.write(0x1000, 1, 32)
        result = struct.unpack_from("<I", self.memory.read(self.controller.cq[0].base, 4))[0]
        self.assertEqual(result, 0)
        self.assertEqual(self.controller.MAX_Q, 1)


if __name__ == "__main__":
    unittest.main()
