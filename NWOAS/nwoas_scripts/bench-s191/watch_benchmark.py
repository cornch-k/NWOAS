"""Live foreground benchmark observation, not a scheduled task.
Only queues read-only observations after the prior worker job has completed.
Stops on observed benchmark completion, worker failure, or bounded host duration.
"""
import json,subprocess,time,sys
from pathlib import Path
root=Path('/Volumes/X31/NWOAS/nwoas_scripts');link=Path(sys.argv[1]);deadline=time.monotonic()+int(sys.argv[2])
while time.monotonic()<deadline:
 # queue_job.py independently refuses to replace a pending command.
 proc=subprocess.run([sys.executable,str(root/'transport-s123/queue_job.py'),str(link),str(root/'bench-s191/inspect.cmd')],capture_output=True,text=True)
 if proc.returncode: print('QUEUE_STOP',proc.stdout,proc.stderr,flush=True);break
 job=int(proc.stdout.strip().split()[-1]);end=time.monotonic()+90
 while time.monotonic()<end:
  events=[json.loads(x) for x in (link/'events.jsonl').read_text().splitlines() if x]
  done=next((e for e in reversed(events) if e.get('job')==job and e.get('kind')==3),None)
  if done:break
  time.sleep(1)
 else: print('OBSERVER_STOP worker completion not seen',job,flush=True);break
 text=(link/f'job-{job}.log').read_text(errors='replace')
 print('HOST_UTC',time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'JOB',job,done,flush=True);print(text,flush=True)
 if done.get('exit') or 'CB_EXIT=' in text or 'CB_TIMEOUT' in text:break
 time.sleep(min(120,max(0,deadline-time.monotonic())))
