"""Execute real bounded methods with fake MMIO/SMC objects, no hardware."""
import sys
from pathlib import Path
from types import SimpleNamespace as NS
sys.path.insert(0,str(Path(__file__).parent))
from bounded_smc import BoundedSMC,BoundedEndpoint
from m1n1.fw.smc import SMCReadKey,SMCWriteKey,SMCInitialize
class Clock:
 def __init__(self):self.t=0
 def __call__(self):self.t+=.01;return self.t
class Ctrl:
 def __init__(self,busy=False):self.reg=NS(FULL=busy)
class Val:
 def __init__(self):self.val=None

def smc(busy=False):
 s=BoundedSMC.__new__(BoundedSMC);s.clock=Clock();s.deadline=0
 s.asc=NS(INBOX_CTRL=Ctrl(busy),INBOX0=Val(),INBOX1=Val())
 s.set_budget(.1);return s
s=smc();s.send(123,32);assert s.asc.INBOX0.val==123 and s.asc.INBOX1.val==32
s=smc(True)
try:s.send(123,32);assert False
except TimeoutError:pass
assert s.asc.INBOX0.val is None
s=smc();s.deadline=0
try:s.send(123,32);assert False
except TimeoutError:pass
for n in [-1,0,6]:
 try:s.set_budget(n);assert False
 except ValueError:pass
class FakeASC:
 def __init__(self):self.calls=0;self.reads=0;self.iface=NS(readmem=self.readmem)
 def check_deadline(self):pass
 def work(self):
  self.calls+=1
  if self.calls>10:raise TimeoutError('fake deadline')
 def readmem(self,a,n):self.reads+=1;return b'ABCDEF'
e=BoundedEndpoint.__new__(BoundedEndpoint);e.asc=FakeASC();e.msgid=0;e.shmem=None;e.send=lambda m:None
try:e.start();assert False
except TimeoutError:pass
assert e.msgid==1 and e.asc.calls==11
e.asc.calls=0;e.outstanding=set();e.ret={}
try:e.cmd(SMCWriteKey());assert False
except ValueError:pass
assert not e.outstanding
try:e.cmd(SMCReadKey());assert False
except TimeoutError:pass
assert e.asc.calls==11
for addr,size in [(0x1000,6),(0xfff,6),(0x1ffb,6),(0x1000,5)]:
 e.shmem=addr;e.cmd=lambda m: NS(SIZE=size)
 e.asc.reads=0
 if addr==0x1000 and size==6:assert e.read_clkm(0x1000,0x1000)==b'ABCDEF' and e.asc.reads==1
 else:
  try:e.read_clkm(0x1000,0x1000);assert False
  except ValueError:pass
  assert e.asc.reads==0
print('PASS bounded SMC sends, deadlines, opcode restriction, SRAM and length guards')
