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
wait(None,180);queue(HERE/'reboot-check.cmd',1);wait(1,180)
t=(LINK/'job-1.log').read_text(errors='replace');assert 'S230 PERSISTED TEST FILE PASS' in t
import re
def num(p):
 m=re.search(p,t);assert m,p;return int(m.group(1))
r={'status':'PASS','session':str(LINK),'job':1,'normal_windows_restart':True,'persisted_test_file_bytes':268435456,'persisted_test_file_sha256':'eb11897202f621134a7ec3c737c4ac3fbeee61ef69faa50d4b9469d8998538d6','cpu_count':num(r'NumberOfLogicalProcessors\s*:\s*(\d+)'),'memory_bytes':num(r'TotalPhysicalMemory\s*:\s*(\d+)'),'cpu_us':num(r'S140 CPU PASS active=8 threads=8 elapsed_us=(\d+)'),'read64m_us':num(r'S140 DISK PASS active=8 bytes=67108864 elapsed_us=(\d+) failed=0'),'free_c_bytes':num(r'S230 FREE_BYTES=(\d+)'),'native_p12':json.loads((LINK/'native-p12.json').read_text()),'hv_sha256':json.loads((HERE/'manifest.json').read_text())['hv_sha256']}
assert r['cpu_count']==8 and r['memory_bytes']==15081889792 and r['native_p12']['p12_selected']
(HERE/'reboot-result.json').write_text(json.dumps(r,indent=2)+'\n');(HERE/'reboot-evidence.txt').write_text('\n'.join(x.rstrip() for x in t.splitlines())+'\n')
print('REBOOT PERSISTENCE PASS',flush=True)
run(HERE/'query.py',LINK,HERE/'counters-reboot.json')
queue(S/'nvme-s163/soak-active-5min.cmd',2);wait(2,420)
run(S/'validation-s220/collect.py','active',LINK,2,HERE/'active-reboot-result.json')
run(HERE/'query.py',LINK,HERE/'counters-final.json')
r=json.loads((HERE/'counters-final.json').read_text());assert r['values']['22']==1 and r['values']['3']>>32==0
print('SECOND BOOT ACTIVE QUALIFICATION PASS',flush=True)
