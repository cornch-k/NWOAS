from pathlib import Path
import os,tempfile,time,json,struct,types
from m1n1.adt import load_adt
r=Path(__file__).resolve().parent
adt=load_adt(Path('/Volumes/X31/NWOAS/experiments/tahoe-afk-20260906/current.adt').read_bytes())
base=0x23d0d9300;counter=123456789012;offset=((int(time.time())<<15)-counter)&((1<<48)-1)
class Bus:
 def __init__(self):self.q=[];self.w=[];self.n=0
 def read32(self,a):
  if a==base:return 0 if self.q else 1<<24
  assert a==base+8;return self.q.pop(0)
 def write32(self,a,v):
  assert a==base+4 and not self.q;self.w.append((a,v));reg=v>>16
  value=offset if reg==0xd100 else counter+self.n*16;self.n+=1
  b=value.to_bytes(6,'little')+b'\0\0';self.q=[0,*struct.unpack('<II',b)]
class U:
 def __init__(self):self.adt=adt;self.tick=100000000
 def mrs(self,n):
  if n=='CNTFRQ_EL0':return 24000000
  assert n=='CNTPCT_EL0';self.tick+=48000;return self.tick
old=dict(os.environ)
try:
 with tempfile.TemporaryDirectory() as d:
  os.environ['NWOAS_RTC_SNAPSHOT']='1';os.environ['NWOAS_LINK_DIR']=d
  for wrong in (False,True):
   adt['/chosen']._properties['chip-id']=0x8104 if wrong else 0x8103
   before=adt.build();bus=Bus();h=types.SimpleNamespace(u=U(),log=lambda s:None)
   exec(compile((r/'probe_bounded.py').read_text(),str(r/'probe_bounded.py'),'exec'),{'hv':h,'p':bus})
   rec=json.loads((Path(d)/'rtc-bounded-probe.json').read_text())
   assert adt.build()==before and rec['guest_adt_changed'] is False
   assert len(bus.w)==(0 if wrong else 3)
   assert ('error' in rec)==wrong
   if not wrong:assert rec['plausible']
finally:
 os.environ.clear();os.environ.update(old)
print('PASS actual module with real J274 ADT: three read transactions, plausible epoch, wrong chip no access, no ADT mutation')
