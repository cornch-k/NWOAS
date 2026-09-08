import unittest,struct
from controller import Controller
from writable_namespace import WindowWritableNamespace
from test_controller import Memory
FIRST,LAST=53839104,59968511
class Test(unittest.TestCase):
 def setUp(self):
  self.m=Memory();self.level=[];self.disk={};self.flushes=0
  def read(lba,n=1):return b''.join(self.disk.get(l,bytes([l%256])*4096) for l in range(lba,lba+n))
  def write(lba,data):
   for i in range(len(data)//4096):self.disk[lba+i]=data[i*4096:(i+1)*4096]
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
 def test_gpt_immutable_in_persistence_trial(self):
  import test_gpt_guard as G
  from gpt_guard import GptGuard
  d=G.Disk();ents=G.entries_of(d);prot=[bytes(ents[i*128:(i+1)*128]) for i in (0,1,3)]
  self.c.ns.gpt=GptGuard(d.read,d.write,61279344,FIRST,LAST,prot)
  for lba in list(range(6))+list(range(61279339,61279344)):
   self.assertEqual(self.st(1,q=1,ns=1,prp=0x8000,dw=(lba,0,0)),0x182)
  self.assertEqual(d.writes,[]);self.assertEqual(self.c.ns.gpt.shadow,{})
 def test_direct_path_dispatch_and_fallback(self):
  calls=[]
  def direct(write,lba,n,prp1,prp2):
   calls.append((write,lba,n,prp1,prp2));return prp1!=0xb000 # 0xb000 -> "not handled"
  self.c.ns.direct=direct
  self.m.write(0x8000,b'\x11'*4096)
  self.assertEqual(self.st(1,q=1,ns=1,prp=0x8000,dw=(FIRST&0xffffffff,FIRST>>32,0)),0)
  self.assertEqual(calls[-1],(True,FIRST,1,0x8000,0));self.assertEqual(self.disk,{}) # DMA'd by device, no host copy
  self.assertEqual(self.st(2,q=1,ns=1,prp=0xa000,dw=(FIRST&0xffffffff,FIRST>>32,0)),0)
  self.assertEqual(calls[-1],(False,FIRST,1,0xa000,0));self.assertNotEqual(self.m.read(0xa000,1),b'\x11') # guest RAM untouched by host
  self.m.write(0xb000,b'\x22'*4096)
  self.assertEqual(self.st(1,q=1,ns=1,prp=0xb000,dw=(FIRST&0xffffffff,FIRST>>32,0)),0)
  self.assertEqual(self.disk,{FIRST:b'\x22'*4096}) # refused by direct -> copy path wrote it
  n=len(calls);self.assertEqual(self.st(1,q=1,ns=1,prp=0x8010,dw=(FIRST&0xffffffff,FIRST>>32,0)),2);self.assertEqual(len(calls),n) # unaligned: no direct attempt
  self.assertEqual(self.st(1,q=1,ns=1,prp=0x8000,dw=(6,0,0)),0x182);self.assertEqual(len(calls),n) # out of zone: never reaches direct
  self.assertEqual(self.st(1,q=1,ns=1,prp=0x8000,prp2=0x9000,dw=(FIRST&0xffffffff,FIRST>>32,1)),0);self.assertEqual(calls[-1][2:],(2,0x8000,0x9000))
 def test_identify_not_write_protected(self):
  self.assertEqual(self.st(6,ns=1,prp=0x8000,dw=(0,)),0);self.assertEqual(self.m.read(0x8000+99,1),b'\x00')
  self.assertEqual(self.m.read(0x8000+130,1),b'\x0c')
if __name__=='__main__':unittest.main()
