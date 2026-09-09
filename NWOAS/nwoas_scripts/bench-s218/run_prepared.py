"""Foreground one-shot upload/launch sequence; never schedules or reboots.
Requires successful S216 soak and the explicitly queued archive job 4.
Every next write waits for acknowledged exit 0 of the previous command.
"""
import json,subprocess,sys,time
from pathlib import Path
S=Path(__file__).resolve().parents[1];link=Path(sys.argv[1])
soak=json.loads((S/'firmware-s216/soak-result.json').read_text());assert soak['pass_'] and Path(soak['session']).resolve()==link.resolve()
def wait(job):
 end=time.monotonic()+240
 while time.monotonic()<end:
  events=[json.loads(x) for x in (link/'events.jsonl').read_text().splitlines() if x]
  d=next((e for e in reversed(events) if e.get('kind')==3 and e.get('job')==job),None)
  if d:
   print('COMPLETE',d,flush=True);assert d.get('exit')==0,d;return
  time.sleep(1)
 raise TimeoutError(job)
wait(4)
for name in ['upload-01.cmd','upload-02.cmd','upload-03.cmd','upload-04.cmd','install-launch.cmd']:
 r=subprocess.run([sys.executable,str(S/'transport-s123/queue_job.py'),str(link),str(S/'bench-s218'/name)],capture_output=True,text=True,check=True)
 job=int(r.stdout.strip().split()[-1]);print('QUEUED',job,name,flush=True);wait(job)
print('BENCHMARK LAUNCH ACKNOWLEDGED; native completion still pending',flush=True)
