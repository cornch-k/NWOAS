import unittest,struct
import test_writable
from writable_namespace import WRITE_ERROR
class Cache(unittest.TestCase):
 setUp=test_writable.Test.setUp
 submit=test_writable.Test.submit
 ack=test_writable.Test.ack
 st=test_writable.Test.st
 def write(self,fua=False):return self.st(1,q=1,ns=1,prp=0x8000,dw=(test_writable.FIRST,0,(1<<30) if fua else 0))
 def test_regular_write_cached_fua_durable(self):
  self.assertEqual(self.write(),0);self.assertEqual(self.flushes,0)
  self.assertEqual(self.write(True),0);self.assertEqual(self.flushes,1)
 def test_disable_flushes_prior_and_future_writes(self):
  self.write();self.assertEqual(self.st(9,dw=(6,0)),0);self.assertEqual(self.flushes,1)
  self.assertEqual(self.write(),0);self.assertEqual(self.flushes,2)
  self.assertEqual(self.c.ns.get_cache(),0)
  self.assertEqual(self.st(9,dw=(6,1)),0);self.write();self.assertEqual(self.flushes,2)
 def test_failed_disable_stays_enabled(self):
  def fail():raise OSError('flush failure')
  self.c.ns.flush=fail
  self.assertNotEqual(self.st(9,dw=(6,0)),0);self.assertEqual(self.c.ns.get_cache(),1)
  self.assertEqual(self.write(True),WRITE_ERROR)
 def test_direct_fua_flush_order(self):
  events=[];self.c.ns.direct=lambda *args:events.append('dma') or True;self.c.ns.flush=lambda:events.append('flush')
  self.assertEqual(self.write(True),0);self.assertEqual(events,['dma','flush'])
 def test_shutdown_ack_order_and_failure(self):
  states=[];self.c.ns.flush=lambda:states.append(self.c.csts&12)
  self.c.write(0x14,self.c.cc|(1<<14),32);self.assertEqual(states,[4]);self.assertEqual(self.c.csts&12,8)
  self.c.write(0x14,self.c.cc,32);self.assertEqual(states,[4])
 def test_shutdown_failure_not_complete(self):
  def fail():raise TimeoutError('flush failed')
  self.c.ns.flush=fail;self.c.write(0x14,self.c.cc|(1<<14),32)
  self.assertEqual(self.c.csts&12,4);self.assertTrue(self.c.csts&2)
 def test_identify_cache_and_features(self):
  self.assertEqual(self.st(6,prp=0x8000,dw=(1,)),0);self.assertEqual(self.m.read(0x8000+525,1),b'\1')
  for sel,expected in [(0,1),(1,1),(3,4)]:
   r=self.submit(10,dw=(6|(sel<<8),));self.ack();self.assertEqual(r[0],expected);self.assertEqual(r[-1]>>1,0)
 def test_power_loss_after_fua(self):
  cache={};media={};self.c.ns.write_block=lambda l,d:cache.update({l:d});self.c.ns.flush=lambda:media.update(cache)
  self.m.write(0x8000,b'X'*4096);self.write(True);cache.clear();self.assertEqual(media[test_writable.FIRST],b'X'*4096)
if __name__=='__main__':unittest.main()
