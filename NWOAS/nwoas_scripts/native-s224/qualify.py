"""One foreground active-read -> 30-minute mixed qualification, no scheduler."""
from pathlib import Path
import json,subprocess,sys,time
S=Path(__file__).resolve().parents[1];here=S/'native-s224';link=Path(sys.argv[1])
def wait(job,seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  events=[json.loads(x) for x in (link/'events.jsonl').read_text().splitlines() if x]
  done=next((x for x in reversed(events) if x.get('job')==job and x.get('kind')==3),None)
  if done:
   assert done.get('exit')==0,done
   print('Completed',job,flush=True);return
  time.sleep(1)
 raise TimeoutError(job)
def run(*args):subprocess.run([sys.executable,*map(str,args)],check=True)
wait(3,420)
run(S/'validation-s220/collect.py','active',link,3,here/'active-result.json')
run(here/'query.py',link,here/'counters-after-active.json')
r=subprocess.run([sys.executable,str(S/'transport-s123/queue_job.py'),str(link),str(S/'nvme-s160/soak-30min.cmd')],capture_output=True,text=True,check=True)
job=int(r.stdout.strip().split()[-1]);assert job==4
print('Queued 30-minute mixed job',job,flush=True);wait(job,2100)
r=subprocess.run([sys.executable,str(S/'nvme-s163/assess_soak.py'),str(link),str(job),'--samples','31','--minimum-span-seconds','1800'],capture_output=True,text=True,check=True)
(here/'soak-result.json').write_text(r.stdout)
print('30-minute mixed qualification PASS',flush=True)
