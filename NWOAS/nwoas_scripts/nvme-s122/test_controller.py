import unittest,struct
from controller import Controller
from readonly_namespace import ReadOnlyNamespace
class Memory:
 def __init__(self):self.b=bytearray(0x20000)
 def contains(self,a,n):return a>=0x1000 and n>0 and a+n<=len(self.b)
 def read(self,a,n):
  if not self.contains(a,n):raise ValueError('bad RAM read')
  return bytes(self.b[a:a+n])
 def write(self,a,b):
  if not self.contains(a,len(b)):raise ValueError('bad RAM write')
  self.b[a:a+len(b)]=b
class Test(unittest.TestCase):
 def setUp(self):
  self.m=Memory();self.level=[];self.reads=[]
  def read(lba,n=1):self.reads.extend(range(lba,lba+n));return b''.join(bytes([l%256])*4096 for l in range(lba,lba+n))
  self.c=Controller(ReadOnlyNamespace(61279344,read),self.m,self.level.append)
  self.c.pci_write(4,6,16)
  self.c.write(0x24,3|(3<<16),32);self.c.write(0x28,0x1000,64);self.c.write(0x30,0x2000,64);self.c.write(0x14,1|(6<<16)|(4<<20),32)
  self.assertEqual(self.c.csts,1)
 def submit(self,op,q=0,ns=0,prp=0,dw=(),cid=7):
  b=bytearray(64);struct.pack_into('<BBHI',b,0,op,0,cid,ns);struct.pack_into('<Q',b,24,prp)
  for i,v in enumerate(dw):struct.pack_into('<I',b,40+4*i,v)
  sq=self.c.sq[q];cq=self.c.cq[sq.cqid];at=cq.tail
  self.m.write(sq.base+sq.tail*64,b);self.c.write(0x1000+q*8,(sq.tail+1)%sq.size,32)
  return struct.unpack('<IIHHHH',self.m.read(cq.base+at*16,16))
 def ack(self,q=0):
  cq=self.c.cq[q];self.c.write(0x1004+8*q,cq.tail,32)
 def queues(self):
  self.assertEqual(self.submit(5,prp=0x4000,dw=(1|(3<<16),3))[-1]>>1,0);self.ack()
  self.assertEqual(self.submit(1,prp=0x3000,dw=(1|(3<<16),1|(1<<16)))[-1]>>1,0);self.ack()
 def test_identify_and_irq(self):
  cqe=self.submit(6,prp=0x8000,dw=(1,));self.assertEqual(cqe[4],7);self.assertEqual(cqe[-1],1)
  self.assertTrue(self.level[-1]);self.assertEqual(self.m.read(0x8004,5),b'NWOAS')
  self.c.write(0xc,1,32);self.assertFalse(self.level[-1]);self.c.write(0x10,1,32);self.assertTrue(self.level[-1]);self.ack();self.assertFalse(self.level[-1])
 def test_read_and_reject_write(self):
  self.queues();self.assertEqual(self.submit(2,q=1,ns=1,prp=0x8000,dw=(1,0,0))[-1]>>1,0);self.ack(1)
  self.assertEqual(self.m.read(0x8000,4096),bytes([1])*4096);self.assertEqual(self.reads,[1])
  self.assertEqual(self.submit(1,q=1,ns=1,prp=0x8000,dw=(1,0,0))[-1]>>1,0x182);self.assertEqual(self.reads,[1])
 def test_phase_wrap(self):
  for i in range(12):
   e=self.submit(10,dw=(7,),cid=i);self.assertEqual(e[-1]&1,1^((i//4)&1));self.assertEqual(e[4],i);self.ack()
 def test_backpressure(self):
  for _ in range(3):self.submit(10,dw=(7,))
  self.submit(10,dw=(7,));self.assertEqual(self.c.cq[0].pending,3);self.assertNotEqual(self.c.sq[0].head,self.c.sq[0].tail)
  self.ack();self.assertEqual(self.c.cq[0].pending,1);self.assertEqual(self.c.sq[0].head,self.c.sq[0].tail)
 def test_invalid_prp_no_guest_write(self):
  original=bytes(self.m.b[0x8000:]);e=self.submit(6,prp=0x700100000,dw=(1,));self.assertEqual(e[-1]>>1,2);self.assertEqual(bytes(self.m.b[0x8000:]),original)
 def test_bad_queue_rejected(self):self.assertEqual(self.submit(5,prp=0x700100000,dw=(1|(3<<16),3))[-1]>>1,2)
 def test_reset(self):
  self.submit(10,dw=(7,));self.c.write(0x14,0,32);self.assertFalse(self.level[-1]);self.assertFalse(self.c.sq);self.assertEqual(self.c.csts,0)
 def test_windows_admin_256(self):
  self.c.write(0x14,0,32);self.c.write(0x24,0xff00ff,32)
  self.c.write(0x28,0x4000,64);self.c.write(0x30,0x8000,64);self.c.write(0x14,0x460001,32)
  self.assertEqual(self.c.csts,1);self.assertEqual(self.c.read(0,64)&0xffff,255)
  self.assertEqual(self.c.sq[0].size,256)
 def test_pci(self):
  self.assertEqual(self.c.pci_read(0,32),0x00101234);self.c.pci_write(0x10,0xffffffff,32);self.assertEqual(self.c.pci_read(0x10,32),0xffffc004)
  self.c.pci_write(0x10,0x00100004,32);self.assertEqual(self.c.pci_read(0x10,32),0x00100004)
if __name__=='__main__':unittest.main()
