"""Actual S160 host callback -> S139 admin owner -> S224 C projection integration.
Fake guest memory and physical backend. No device or file data I/O.
"""
from pathlib import Path
from types import SimpleNamespace,ModuleType
import ast,ctypes as C,importlib.util,json,struct,sys,time
S=Path(__file__).resolve().parent
sys.path.insert(0,str(S.parent/'nvme-s124'))
spec=importlib.util.spec_from_file_location('s224_owner_ref',S.parent/'nvme-s139/controller.py');ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
mods={n:ModuleType(n) for n in ['m1n1','m1n1.hv','m1n1.hv.types','m1n1.utils']};mods['m1n1.hv.types'].TraceMode=SimpleNamespace(HOOK=1);mods['m1n1.utils'].irange=lambda a,n:(a,n);sys.modules.update(mods)
lib=C.CDLL(str(S/'out/projection.dylib'));lib.publish.argtypes=[C.c_uint64]*5;lib.read_projection.argtypes=[C.c_uint64,C.c_uint,C.c_int,C.c_int,C.c_uint32,C.POINTER(C.c_uint64)]
class Mem:
 def __init__(self):self.b=bytearray(0x100000)
 def contains(self,a,n):return 0<a<=len(self.b) and n<=len(self.b)-a
 def read(self,a,n):assert self.contains(a,n);return bytes(self.b[a:a+n])
 def write(self,a,b):assert self.contains(a,len(b));self.b[a:a+len(b)]=b
class NS:
 cache_enabled=True
 def __init__(self):self.flushes=0
 def flush(self):self.flushes+=1
class P:
 def __init__(self):self.armed=False;self.mask=0;self.flags=0;self.events=[]
 def nwoas_nvme_fastpath(self,a,sq=0,n=0,cq=0,m=0,flags=0):
  self.events.append(a)
  if a==12:return 0x5332323400000001
  if a==11:lib.publish(sq,n,cq,m,flags);return 0x5332323400000001
  if a==8:return (0x53313630<<32)|self.mask
  if a==0:self.armed=False;return 1
  if a in (1,2):
   self.mask=flags>>32;self.flags=flags
   if a==1:self.armed=True
   return int(self.armed)
  raise AssertionError(a)
class H:
 def __init__(self,c):self._nwoas_nvme=(c,None);self.tracers={}
 def add_tracer(self,z,k,m,**kw):self.tracers[k]=kw
 def log(self,x):pass
memory=Mem();ns=NS();c=ref.Controller(ns,memory,lambda x:None);p=P();h=H(c)
g=dict(p=p,c=c,hv=h,ECAM=0x700000000,BAR=0x700100000,_namespace=ns,fast_armed=False,FASTPATH_ENABLED=True,FAST_MASK_ENABLED=True,FAST_MASK_SIGNATURE=0x53313630,fast_sq_generation=0,fast_cq_generation=0,log=lambda x:None,_t=time,st=dict(host=0.,guest=0.,traps=0,db=0,last_exit=None))
tree=ast.parse((S.parent/'nvme-s160/guest_module.py').read_text());names={'_fast_ready','_fast_pull_mask','_fast_flags','_fast_disable','_fast_sync','_fast_arm_if_ready','pci_read','pci_write','_enter','_leave','mmio_read','mmio_write'}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[]),'actual_s160_callbacks','exec'),g)
exec(compile((S/'module.py').read_text(),'actual_s224_overlay','exec'),g)
checks=0

def check():
 global checks
 for pci in [True,False]:
  for off,w in ([(0,3),(4,2),(0x10,3),(0x3c,2)] if pci else [(0,3),(0xc,2),(0x10,2),(0x14,2),(0x1c,2),(0x24,2),(0x28,3),(0x30,3)]):
   mask=c.mask;c.mask=p.mask if p.armed else mask
   want=(c.pci_read if pci else c.read)(off,8<<w);c.mask=mask
   out=C.c_uint64();assert lib.read_projection((0x700000000 if pci else 0x700100000)+off,w,p.armed,False,p.mask,C.byref(out));assert out.value==want,(pci,off,out.value,want);checks+=1

def write(off,v,w=32,pci=False):
 h.tracers['s93-nvme-ecam' if pci else 's93-nvme-bar']['write']((0x700000000 if pci else 0x700100000)+off,v,w);check()
write(0x24,0x00ff00ff);write(0x28,0x10000,64);write(0x30,0x20000,64);write(0x14,0x460001)
assert c.csts==1 and not p.armed
idx=0

def command(op,dw10,dw11=0,prp=0):
 global idx
 b=bytearray(64);b[0]=op;struct.pack_into('<H',b,2,idx+1);struct.pack_into('<Q',b,24,prp);struct.pack_into('<II',b,40,dw10,dw11);memory.write(0x10000+idx*64,b);idx+=1;write(0x1000,idx)
 assert c.cq[0].pending==1;write(0x1004,c.cq[0].tail);assert not c.cq[0].pending
command(5,(255<<16)|1,3,0x30000)
command(1,(255<<16)|1,(1<<16)|1,0x40000)
assert p.armed and g['fast_armed']
# Simulate an existing target-local mask write. The mirror must use that value
# immediately, and the next host-owned write must pull it before publishing.
p.mask=1;check();write(4,0x400,16,True);assert c.mask==1
write(0x10,1);assert p.mask==0 and c.mask==0
command(0,1);assert not p.armed
command(4,1);assert 1 not in c.cq
write(0x14,0x464001);assert ns.flushes==1 and c.csts==9
write(0x14,0);assert c.csts==0 and not c.sq and not c.cq
r={'pass_':True,'compared_reads':checks,'actual_source':'S160 callback functions, S139 Controller, S224 overlay and compiled C mirror','covered':['admin queue enable','create CQ/SQ','pending completion and acknowledgement','fastpath arm/disarm','target mask -> host state synchronization','PCI INTx mask','delete SQ/CQ','shutdown flush','CC disable'],'scope':'Fake physical backend, not hardware'}
(S/'owner-integration-result.json').write_text(json.dumps(r,indent=2)+'\n');print(r)
