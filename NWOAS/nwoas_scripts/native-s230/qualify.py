"""One foreground qualification sequence; no scheduler or reboot actions."""
from pathlib import Path
import json,subprocess,sys,time
S=Path(__file__).resolve().parents[1];HERE=S/'native-s230';LINK=Path(sys.argv[1])
def events():
 p=LINK/'events.jsonl';return [json.loads(x) for x in p.read_text().splitlines()] if p.exists() else []
def wait(job,seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  e=events()
  if job is None and any(x.get('kind')==4 for x in e):return
  d=next((x for x in e if x.get('kind')==3 and x.get('job')==job),None)
  if d:assert d.get('exit')==0,d;print('Completed',job,flush=True);return
  time.sleep(1)
 raise TimeoutError(job)
def run(*args):subprocess.run([sys.executable,*map(str,args)],check=True)
def queue(script,expected):
 r=subprocess.run([sys.executable,str(S/'transport-s123/queue_job.py'),str(LINK),str(script)],capture_output=True,text=True,check=True)
 assert int(r.stdout.strip().split()[-1])==expected;print('Queued',expected,flush=True)
wait(None,120);queue(HERE/'integration.cmd',1);wait(1,120)
run(S/'validation-s220/collect.py','integration',LINK,1,HERE/'hardware-result.json')
t=(LINK/'job-1.log').read_text(errors='replace');assert 'S229 PERSISTENCE PASS' in t
(HERE/'integration-evidence.txt').write_text('\n'.join(x.rstrip() for x in t.splitlines())+'\n')
run(HERE/'query.py',LINK,HERE/'counters-first.json')
r=json.loads((HERE/'counters-first.json').read_text());assert r['values']['22']==1 and r['values']['3']>>32==0 and r['values']['20']>0
queue(S/'memory-s180/full-memory.cmd',2);wait(2,180)
t=(LINK/'job-2.log').read_text(errors='replace');(HERE/'memory-evidence.txt').write_text('\n'.join(x.rstrip() for x in t.splitlines())+'\n')
queue(HERE/'soak-10min.cmd',3);wait(3,720)
r=subprocess.run([sys.executable,str(S/'nvme-s163/assess_soak.py'),str(LINK),'3','--samples','11','--minimum-span-seconds','600','--completion-marker','TEN MINUTE READ-MOSTLY SOAK PASS'],capture_output=True,text=True,check=True)
(HERE/'soak-result.json').write_text(r.stdout);print('TEN-MINUTE QUALIFICATION PASS',flush=True)
