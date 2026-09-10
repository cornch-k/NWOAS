"""Request only named read-only S224 counters through existing NS2 service."""
import argparse,json,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('link',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
ident='s224-'+str(time.time_ns());req=a.link/'target-query.json';temp=req.with_suffix('.tmp')
temp.write_text(json.dumps({'id':ident,'actions':[3,4,12,13,14,15]})+'\n');temp.replace(req)
end=time.monotonic()+30
while time.monotonic()<end:
 result=a.link/'target-query-result.json'
 if result.exists():
  r=json.loads(result.read_text())
  if r.get('id')==ident:
   assert 'error' not in r,r
   assert r['values']['12']==0x5332323400000001,r
   a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));break
 time.sleep(.5)
else:raise TimeoutError('S224 read-only query')
