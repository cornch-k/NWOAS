import unittest,struct
from controller import Controller
from writable_namespace import WindowWritableNamespace
from test_controller import Memory
FIRST,LAST=53839104,59968511
class Test(unittest.TestCase):
 def setUp(self):
  self.m=Memory();self.level=[];self.disk={};self.flushes=0
  def read(lba):return self.disk.get(lba,bytes([lba%256])*4096)
  def write(lba,data):self.disk[lba]=data
  def flush():self.flushes+=1
  self.c=Controller(WindowWritableNamespace(61279344,read,write,flush,FIRST,LAST),self.m,self.level.append)
  self.c.pci_write(4,6,16)
  self.c.write(0x24,3|(3<<16),32);self.c.write(0x28,0x1000,64);self.c.write(0x30,0x2000,64);self.c.write(0x14,1|(6<<16)|(4<<20),32)
  self.assertEqual(self.c.submit if hasattr(self.c,'submit') else 1,1)
  self.assertEqual(self.submit(5,prp=0x4000,dw=(1|(3<<16),3))[-1]>>1,0);self.ack()
  self.assertEqual(self.submit(1,prp=0x3000,dw=(1|(3<<16),1|(1<<16)))[-1]>>1,0);self.ack()
 def submit(self,op,q=0,ns=0,prp=0,dw=(),cid=7,prp2=0):
  b=bytearray(64);struct.pack_into('<BBHI',b,0,op,0,cid,ns);struct.pack_into('<QQ',b,24,prp,prp2)
  for i,v in enumerate(dw):struct.pack_into('<I',b,40+4*i,v)
  sq=self.c.sq[q];cq=self.c.cq[sq.cqid];at=cq.tail
  self.m.write(sq.base+sq.tail*64,b);self.c.write(0x1000+q*8,(sq.tail+1)%sq.size,32)
  return struct.unpack('<IIHHHH',self.m.read(cq.base+at*16,16))
 def ack(self,q=0):
  cq=self.c.cq[q];self.c.write(0x1004+8*q,cq.tail,32)
 def st(self,*a,**k):
  r=self.submit(*a,**k)[-1]>>1;self.ack(k.get('q',0));return r
 def test_write_inside_window_roundtrip(self):
  self.m.write(0x8000,b'\xa5'*8192)
  self.m.write(0x9000,b'\x5a'*4096)
  self.assertEqual(self.st(1,q=1,ns=1,prp=0x8000,prp2=0x9000,dw=(FIRST&0xffffffff,FIRST>>32,1)),0)
  self.assertEqual(self.disk,{FIRST:b'\xa5'*4096,FIRST+1:b'\x5a'*4096})
  self.assertEqual(self.st(2,q=1,ns=1,prp=0xa000,dw=(FIRST&0xffffffff,FIRST>>32,0)),0)
  self.assertEqual(self.m.read(0xa000,4096),b'\xa5'*4096)
 def test_write_at_window_edges(self):
  self.assertEqual(self.st(1,q=1,ns=1,prp=0x8000,dw=(LAST&0xffffffff,LAST>>32,0)),0)
  self.assertEqual(self.st(1,q=1,ns=1,prp=0x8000,prp2=0x9000,dw=((LAST-1)&0xffffffff,LAST>>32,1)),0)
  self.assertEqual(set(self.disk),{LAST-1,LAST})
 def test_write_outside_or_straddling_rejected(self):
  for slba,nlb in ((0,0),(FIRST-1,0),(FIRST-1,1),(LAST,1),(LAST+1,0),(53838942,0),(59968630,0)):
   self.assertEqual(self.st(1,q=1,ns=1,prp=0x8000,dw=(slba&0xffffffff,slba>>32,nlb)),0x182,(slba,nlb))
  self.assertEqual(self.disk,{})
 def test_other_mutating_opcodes_rejected(self):
  for op in (0x04,0x08,0x09,0x0d):
   self.assertEqual(self.st(op,q=1,ns=1,prp=0x8000,dw=(FIRST&0xffffffff,FIRST>>32,0)),0x182)
  self.assertEqual(self.disk,{})
 def test_flush(self):
  self.assertEqual(self.st(0,q=1,ns=1),0);self.assertEqual(self.flushes,1)
  self.assertEqual(self.st(0,q=1,ns=2),0x0b)
 def test_invalid_prp_no_write(self):
  self.assertEqual(self.st(1,q=1,ns=1,prp=0x700100000,dw=(FIRST&0xffffffff,FIRST>>32,0)),2);self.assertEqual(self.disk,{})
 def test_identify_not_write_protected(self):
  self.assertEqual(self.st(6,ns=1,prp=0x8000,dw=(0,)),0);self.assertEqual(self.m.read(0x8000+99,1),b'\x00')
  self.assertEqual(self.m.read(0x8000+130,1),b'\x0c')
if __name__=='__main__':unittest.main()
