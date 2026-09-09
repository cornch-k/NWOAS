"""Differential checks against the actual S139 Python controller, no hardware."""
from pathlib import Path
import ctypes as C
import importlib.util
import random
import sys
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'nvme-s124'))
spec=importlib.util.spec_from_file_location('s209_reference',HERE.parent/'nvme-s139/controller.py')
ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
lib=C.CDLL(str(HERE/'out/model.dylib'))
lib.test_admin.argtypes=[C.c_uint];lib.test_admin.restype=C.c_uint64
lib.test_memory.argtypes=[C.c_uint64,C.c_uint64]
lib.test_write.argtypes=[C.c_int,C.c_uint32,C.c_uint,C.c_uint64]
lib.test_read.argtypes=[C.c_int,C.c_uint32,C.c_uint];lib.test_read.restype=C.c_uint64
class Mem:
 def __init__(self):self.low=0x10000;self.high=0x100000
 def contains(self,a,n):return self.low<=a<=self.high and n<=self.high-a
class NS:
 def __init__(self):self.calls=0;self.fail=False
 def flush(self):
  self.calls+=1
  if self.fail:raise OSError('injected')
class Pair:
 def __init__(self):
  self.irq=[];self.ns=NS();self.p=ref.Controller(self.ns,Mem(),self.irq.append);lib.test_init();self.steps=0
 def check(self):
  for off,w in [(0,64),(4,32),(8,32),(0x10,64),(0x2c,32),(0x3c,32),(4095,8)]:
   assert lib.test_read(1,off,w)==self.p.pci_read(off,w),(self.steps,'PCI',off)
  for off,w in [(0,64),(8,32),(12,32),(16,32),(20,32),(28,32),(36,32),(40,64),(48,64),(0x3fff,8)]:
   assert lib.test_read(0,off,w)==self.p.read(off,w),(self.steps,'reg',off,hex(lib.test_read(0,off,w)),hex(self.p.read(off,w)))
  assert lib.test_counter(0)==self.irq[-1],(self.steps,'IRQ')
  assert lib.test_counter(1)==len(self.irq),(self.steps,'IRQ count',lib.test_counter(1),len(self.irq))
  assert lib.test_counter(4)==self.ns.calls,(self.steps,'flush')
  assert lib.test_admin(0)==(0 in self.p.sq),(self.steps,'admin queue active')
  if 0 in self.p.sq:
   assert [lib.test_admin(x) for x in (1,2,3,4)]==[self.p.sq[0].base,self.p.sq[0].size,self.p.cq[0].base,self.p.cq[0].size],(self.steps,'queue descriptor')
 def write(self,pci,off,w,v):
  status=lib.test_write(pci,off,w,v);assert status==0,(pci,off,w,v,status)
  (self.p.pci_write if pci else self.p.write)(off,v,w);self.steps+=1;self.check()
 def pending(self,p):
  if 1 not in self.p.cq:self.p.cq[1]=ref.CQ(0x90000,256)
  for q in self.p.cq.values():q.pending=int(p);q.ien=True
  self.p.update_irq();lib.test_pending(p);self.check()
 def setup(self):
  self.write(0,0x24,32,0x00ff00ff);self.write(0,0x28,64,0x10000);self.write(0,0x30,64,0x20000)
  self.write(0,0x14,32,0x00460001)
p=Pair();p.check()
# Defined arbitrary PCI slices including command overlap, offset-5 quirk, BAR probes.
for off in range(32):
 for w in (8,16,32,64):p.write(1,off,w,(1<<w)-1)
for off in (0x10,0x14):
 for v in (0xffffffff,0,0x100004,7):p.write(1,off,32,v)
p=Pair();p.setup();assert lib.test_counter(2)==1
p.pending(True);p.write(0,12,32,1);p.write(0,16,32,1)
p.write(1,4,16,0x400);p.write(1,4,16,7)
p.write(0,0x28,64,0x30000);assert p.p.asq==0x10000
p.write(0,0x14,32,0x00464001);assert p.p.csts==9 and p.ns.calls==1
p.write(0,0x14,32,0x00464001);assert p.ns.calls==1
p.write(0,0x14,32,0);assert not p.p.cq
for aqa,asq,acq,cc in [(0,0x10000,0x20000,0x460001),(0xffff,0x10000,0x20000,0x460001),
 (0xff00ff,0,0x20000,0x460001),(0xff00ff,0x10001,0x20000,0x460001),
 (0xff00ff,0xff000,0x20000,0x460001),(0xff00ff,0x10000,0x20000,0x460081),
 (0xff00ff,0x10000,0x20000,0x460011),(0xff00ff,0x10000,0x20000,0x450001)]:
 p=Pair();p.write(0,0x24,32,aqa);p.write(0,0x28,64,asq);p.write(0,0x30,64,acq);p.write(0,0x14,32,cc)
 assert p.p.csts&2
p=Pair();p.setup();p.ns.fail=True;lib.test_failure(1,0);p.write(0,0x14,32,0x464001);assert p.p.csts&2
# 10,000 seeded accesses over the reference's defined register widths.
rng=random.Random(209);p=Pair()
for i in range(10000):
 action=rng.randrange(8)
 if action==0:p.pending(rng.randrange(2))
 elif action==1:
  p.p.reset();lib.test_reset();p.check()
 elif action==2:p.setup()
 elif action==3:p.write(1,rng.randrange(0,32),rng.choice((8,16,32)),rng.getrandbits(8))
 elif action==4:p.write(0,rng.choice((12,16,20,36)),32,rng.getrandbits(32))
 elif action==5:p.write(0,rng.choice((40,44,48,52)),32,rng.getrandbits(32))
 elif action==6:p.write(0,rng.choice((40,48)),64,rng.getrandbits(64))
 else:p.write(0,20,32,rng.choice((0,0x460001,0x464001,0x468001)))
print('PASS: directed cases and 10,000 seeded stateful differential operations; register/PCI bytes, IRQ levels/counts, flush counts agree')
