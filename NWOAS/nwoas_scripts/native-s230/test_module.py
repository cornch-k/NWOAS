from pathlib import Path
from types import SimpleNamespace as NS
import runpy,json
HERE=Path(__file__).resolve().parent
class HV:
 def __init__(self):
  primary=NS(block_count=61279344,write_first=53839104,write_last=59968629,cache_enabled=True,mdts=8)
  self.controller=NS(cc=0,sq={},cq={},ns=NS(primary=primary,link=NS(block_count=8448)))
  self._nwoas_nvme=(self.controller,object(),0);self.tracers={};self.logs=[];self.nwoas_nvme_link_handler=object()
 def add_tracer(self,span,key,mode,**kw):self.tracers[key]=(span,mode,kw)
 def log(self,s):self.logs.append(s)
class Proxy:
 def __init__(self):self.calls=[];self.cap=0x5332333000000001;self.accept=1
 def nwoas_nvme_fastpath(self,*args):self.calls.append(args);return self.cap if args[0]==23 else self.accept
h=HV();p=Proxy();handler=h.nwoas_nvme_link_handler
runpy.run_path(str(HERE/'module.py'),init_globals={'hv':h,'p':p})
assert p.calls==[(23,),(16,8448,8)] and len(h.tracers)==2 and h.nwoas_nvme_link_handler is handler
assert not h.controller.cc and not h.controller.sq and not h.controller.cq
for span,mode,kw in h.tracers.values():
 for kind,args in [('read',(0x700100000,32)),('write',(0x700100000,0,32))]:
  try:kw[kind](*args)
  except RuntimeError:pass
  else:raise AssertionError('unexpected access silently accepted')
for bad in ['cc','mdts','capacity','window','cache','capability','enable']:
 h=HV();p=Proxy()
 if bad=='cc':h.controller.cc=1
 if bad=='mdts':h.controller.ns.primary.mdts=4
 if bad=='capacity':h.controller.ns.link.block_count=1
 if bad=='window':h.controller.ns.primary.write_last=61279343
 if bad=='cache':h.controller.ns.primary.cache_enabled=False
 if bad=='capability':p.cap=0
 if bad=='enable':p.accept=0
 try:runpy.run_path(str(HERE/'module.py'),init_globals={'hv':h,'p':p})
 except RuntimeError:pass
 else:raise AssertionError(bad)
 assert not h.tracers
r={'pass_':True,'cases':8,'scope':'Fake preboot proxy; exact policy/capability gating, no guest state owner retained, NS2 handler preserved and fallback callbacks fail closed'}
(HERE/'module-result.json').write_text(json.dumps(r,indent=2)+'\n');print(r)
