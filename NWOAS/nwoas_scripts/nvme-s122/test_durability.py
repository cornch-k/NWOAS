import unittest
import test_writable
from writable_namespace import WRITE_ERROR

class Durability(unittest.TestCase):
    setUp = test_writable.Test.setUp
    submit = test_writable.Test.submit
    ack = test_writable.Test.ack
    st = test_writable.Test.st

    def issue_write(self, fua=False):
        first=test_writable.FIRST
        return self.st(1,q=1,ns=1,prp=0x8000,dw=(first&0xffffffff,first>>32,(1<<30) if fua else 0))

    def test_completed_write_survives_volatile_cache_loss(self):
        volatile={};durable={};events=[]
        def write(lba,data):volatile[lba]=data;events.append('write')
        def flush():durable.update(volatile);events.append('flush')
        self.c.ns.write_block=write;self.c.ns.flush=flush
        self.m.write(0x8000,b'\x57'*4096)
        self.assertEqual(self.issue_write(),0)
        volatile.clear()
        self.assertEqual(durable[test_writable.FIRST],b'\x57'*4096)
        self.assertEqual(events,['write','flush'])

    def test_fua_copy_is_flushed(self):
        self.assertEqual(self.issue_write(True),0)
        self.assertEqual(self.flushes,1)

    def test_direct_write_is_flushed_before_success(self):
        events=[]
        def direct(*args):events.append('dma');return True
        self.c.ns.direct=direct;self.c.ns.flush=lambda:events.append('flush')
        self.assertEqual(self.issue_write(True),0)
        self.assertEqual(events,['dma','flush'])

    def test_flush_failure_is_not_success_copy_or_direct(self):
        def fail():raise OSError('injected durability failure')
        self.c.ns.flush=fail
        self.assertEqual(self.issue_write(),WRITE_ERROR)
        self.c.ns.direct=lambda *a:True
        self.assertEqual(self.issue_write(True),WRITE_ERROR)

    def test_invalid_write_never_flushes(self):
        self.assertEqual(self.st(1,q=1,ns=1,prp=0x8000,dw=(0,0,1<<30)),0x182)
        self.assertEqual(self.flushes,0)

    def test_shutdown_complete_only_after_flush(self):
        during=[]
        self.c.ns.flush=lambda:during.append(self.c.csts&12)
        value=self.c.cc|(1<<14)
        self.c.write(0x14,value,32)
        self.assertEqual(during,[4]);self.assertEqual(self.c.csts&12,8)
        self.c.write(0x14,value,32)
        self.assertEqual(during,[4])

    def test_shutdown_flush_failure_never_reports_complete(self):
        def fail():raise TimeoutError('injected shutdown timeout')
        self.c.ns.flush=fail
        self.c.write(0x14,self.c.cc|(1<<14),32)
        self.assertEqual(self.c.csts&12,4);self.assertTrue(self.c.csts&2)

if __name__=='__main__':unittest.main()
