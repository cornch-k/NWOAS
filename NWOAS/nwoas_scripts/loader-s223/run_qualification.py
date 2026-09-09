"""One live qualification sequence: completed integration, 10GiB memory, 15min mixed.
No scheduling, resets, firmware writes, or replacement of pending worker jobs.
"""
import json,subprocess,sys,time
from pathlib import Path
S=Path(__file__).resolve().parents[1];link=Path(sys.argv[1])
def wait(job,limit):
 end=time.monotonic()+limit
 while time.monotonic()<end:
  es=[json.loads(x) for x in (link/'events.jsonl').read_text().splitlines() if x]
  d=next((e for e in reversed(es) if e.get('job')==job and e.get('kind')==3),None)
  if d:
   print('COMPLETE',d,flush=True);assert d.get('exit')==0,d;return
  time.sleep(1)
 raise TimeoutError(job)
wait(1,240)
for script,limit in [(S/'memory-s180/full-memory.cmd',240),(S/'loader-s223/soak-15min.cmd',1100)]:
 r=subprocess.run([sys.executable,str(S/'transport-s123/queue_job.py'),str(link),str(script)],capture_output=True,text=True,check=True);job=int(r.stdout.strip().split()[-1]);print('QUEUED',job,script.name,flush=True);wait(job,limit)
print('S223 BOUNDED QUALIFICATION COMMANDS COMPLETE',flush=True)
