"""Control-access replay, explicitly excludes unimplemented admin/doorbells.
Only parsed register transaction fields are read, never a raw device log.
"""
from pathlib import Path
import json
from test_differential import Pair,lib
p=Pair();p.p.mem.low=0;p.p.mem.high=1<<36;lib.test_memory(0,1<<36)
rows=json.loads((Path(__file__).parent/'s200-register-transactions.json').read_text())['transactions']
reads=writes=doorbells=observed_matches=0;mismatches=[]
for index,r in enumerate(rows):
 pci=int(r['kind']=='PCI');off=r['offset'];w=r['width'];v=r['value']
 if not pci and off>=0x1000:
  assert r['op']=='W' and lib.test_write(0,off,w,v)==1
  doorbells+=1;continue
 if r['op']=='W':p.write(pci,off,w,v);writes+=1
 else:
  cv=lib.test_read(pci,off,w);pv=(p.p.pci_read if pci else p.p.read)(off,w)
  assert cv==pv,(index,r,cv,pv)
  reads+=1
  if cv==v:observed_matches+=1
  else:mismatches.append({'transaction':index,'kind':r['kind'],'offset':off,'observed':v,'replay':cv})
 p.check()
result={'status':'PASS C/Python control-access equivalence','reads':reads,'writes':writes,
 'unimplemented_doorbells_returned_not_handled':doorbells,'logged_read_values_matched':observed_matches,
 'logged_read_value_differences':mismatches,
 'limitation':'Not full controller replay: omitted admin command bodies/completions, target fastpath and capped logging. Matching logged reads does not prove those paths.'}
(Path(__file__).parent/'trace-result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
