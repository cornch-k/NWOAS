"""Regression: a 128-byte ring's producer must not overwrite its read pointer."""
import struct
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'm1n1_windows/proxyclient'))
from m1n1.fw.afk.rbep import AFKRingBuf

class Memory:
    def __init__(self, block):
        self.data = bytearray(0x4000)
        struct.pack_into('<II', self.data, 0, len(self.data) - 3*block, 0x70006)
    def readmem(self, addr, size):
        assert 0 <= addr <= len(self.data) - size
        return bytes(self.data[addr:addr+size])
    def writemem(self, addr, data):
        assert 0 <= addr <= len(self.data) - len(data)
        self.data[addr:addr+len(data)] = data
    def write32(self, addr, value):
        self.writemem(addr, struct.pack('<I', value))

class RingTest(unittest.TestCase):
    def test_pointer_independence(self):
        for block in (64, 128):
            with self.subTest(block=block):
                memory = Memory(block)
                ep = SimpleNamespace(iface=memory, asc=SimpleNamespace(p=memory))
                rb = AFKRingBuf(ep, 0, len(memory.data))
                rb.update_rptr(block)
                rb.update_wptr(3*block)
                self.assertEqual(rb.get_rptr(), block)
                self.assertEqual(rb.get_wptr(), 3*block)
    def test_messages_wrap_without_loss(self):
        for block in (64, 128):
            with self.subTest(block=block):
                memory = Memory(block)
                ep = SimpleNamespace(iface=memory, asc=SimpleNamespace(p=memory))
                producer = AFKRingBuf(ep, 0, len(memory.data))
                consumer = AFKRingBuf(ep, 0, len(memory.data))
                wrapped = False
                previous = 0
                for index in range(200):
                    data = struct.pack('<II', index, 3) + bytes([index % 256]) * (37 + index % 199)
                    position = producer.write(data)
                    wrapped |= position < previous
                    previous = position
                    self.assertEqual(list(consumer.read()), [data])
                    self.assertEqual(producer.get_rptr(), producer.get_wptr())
                self.assertTrue(wrapped)

if __name__ == '__main__':
    unittest.main()
