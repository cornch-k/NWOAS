"""Exercise actual overlay callback wiring, snapshot order and fail-closed path."""
from pathlib import Path
from types import SimpleNamespace,ModuleType
import sys,json
S=Path(__file__).resolve().parent
mods={n:ModuleType(n) for n in ['m1n1','m1n1.hv','m1n1.hv.types','m1n1.utils']}
mods['m1n1.hv.types'].TraceMode=SimpleNamespace(HOOK=1)
mods['m1n1.utils'].irange=lambda a,n:(a,n)
sys.modules.update(mods)
sig=0x5332323400000001
class Proxy:
 def __init__(self):self.calls=[];self.fail_publish=False
 def nwoas_nvme_fastpath(self,action,*args):
  self.calls.append((action,args))
  if action==11 and args and self.fail_publish:return 0
  return sig
class HV:
 def __init__(self,c):self._nwoas_nvme=(c,None);self.tracers={}
 def add_tracer(self,zone,name,mode,**kw):self.tracers[name]=kw
 def log(self,msg):pass

def setup(fault=False):
 c=SimpleNamespace(MAX_Q=1,MAX_DEPTH=256,bar=0x700100000,cc=0,csts=0,aqa=0,mask=0,asq=0,acq=0,command=0,probe_low=False,probe_high=False,cq={})
 p=Proxy();h=HV(c);order=[]
 def pci(a,v,w):order.append('pci');c.command=v
 def mmio(a,v,w):
  order.append('mmio');c.cc=v;c.csts=1;c.cq[0]=SimpleNamespace(ien=True,pending=1)
  if fault:raise RuntimeError('injected owner failure')
 g=dict(hv=h,p=p,pci_write=pci,mmio_write=mmio,pci_read=lambda a,w:0,mmio_read=lambda a,w:0)
 exec(compile((S/'module.py').read_text(),str(S/'module.py'),'exec'),g)
 return c,p,h,order,g
c,p,h,o,g=setup();assert p.calls[0]==(12,()) and p.calls[-1][0]==11
h.tracers['s93-nvme-bar']['write'](0x700100014,0x460001,32)
assert o==['mmio'] and p.calls[-1][1][0]==0x100460001 and p.calls[-1][1][-1]&(1<<16)
h.tracers['s93-nvme-ecam']['write'](0x700000004,0x400,16)
assert o==['mmio','pci'] and p.calls[-1][1][-1]&0xffff==0x400
c,p,h,o,g=setup(True)
try:h.tracers['s93-nvme-bar']['write'](0x700100014,1,32);assert False
except RuntimeError:pass
assert p.calls[-1]==(11,())
c,p,h,o,g=setup();p.fail_publish=True
try:h.tracers['s93-nvme-bar']['write'](0x700100014,1,32);assert False
except RuntimeError:pass
assert p.calls[-1]==(11,())
c,p,h,o,g=setup();c.mask=1<<32
try:h.tracers['s93-nvme-ecam']['write'](0x700000004,0,16);assert False
except ValueError:pass
assert p.calls[-1]==(11,())
r={'pass_':True,'checks':['actual tracer callbacks','publish after owner transition','pending and command reflected','owner exception disables','publication failure disables','invalid dword disables'],'scope':'Python wiring with fake target, no hardware'}
(S/'module-test-result.json').write_text(json.dumps(r,indent=2)+'\n');print(r)
