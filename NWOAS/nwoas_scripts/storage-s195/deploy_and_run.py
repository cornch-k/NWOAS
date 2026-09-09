"""Foreground, single-session S195 deployment. No scheduling or hardware resets.
Waits for the explicitly supplied prior job to finish successfully. Every next
command depends on the previous acknowledged exit 0. Stops on any failure.
"""
import json,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[1]
link=Path(sys.argv[1]);previous=int(sys.argv[2])
def wait(job,seconds=420):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  events=[json.loads(x) for x in (link/'events.jsonl').read_text().splitlines() if x]
  done=next((e for e in reversed(events) if e.get('job')==job and e.get('kind')==3),None)
  if done:
   print('COMPLETE',done,flush=True)
   if done.get('exit')!=0:raise RuntimeError('prior job failed')
   return
  time.sleep(1)
 raise TimeoutError('job completion not observed')
wait(previous)
for path in [*sorted((root/'storage-s195').glob('upload-[0-9][0-9].cmd')),root/'storage-s195/upload-verify.cmd',root/'storage-s195/run.cmd']:
 p=subprocess.run([sys.executable,str(root/'transport-s123/queue_job.py'),str(link),str(path)],capture_output=True,text=True,check=True)
 job=int(p.stdout.strip().split()[-1]);print('QUEUED',job,path.name,flush=True);wait(job)
 if path.name in ('upload-verify.cmd','run.cmd'):print((link/f'job-{job}.log').read_text(errors='replace'),flush=True)
