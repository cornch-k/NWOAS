"""Read-only observer of one live test output; no serial/UI access or scheduling."""
import datetime,json,re,time
from pathlib import Path
link=Path('/Volumes/X31/NWOAS/nwoas_scripts/logs/usb-s160-20260910-000739.m0HOW6.link')
out=link/'soak-host-observations.jsonl'
seen=set();start=time.monotonic()
with out.open('a') as f:
 while time.monotonic()-start<2400:
  p=link/'job-4.log';s=p.read_text(errors='replace') if p.exists() else ''
  for n,t in re.findall(r'S160_SAMPLE (\d+) ([^\s]+)',s):
   if n in seen:continue
   seen.add(n);rec=dict(sample=int(n),guest_time=t,host_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),observer_elapsed=time.monotonic()-start)
   f.write(json.dumps(rec)+'\n');f.flush()
  events=(link/'events.jsonl').read_text() if (link/'events.jsonl').exists() else ''
  if '"job": 4, "kind": 3' in events:break
  time.sleep(1)
print('observed',len(seen),'samples')
