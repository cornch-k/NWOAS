"""Queue one ASCII CMD script atomically in a running S123 session directory."""
import argparse,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('session',type=Path);p.add_argument('script',type=Path);a=p.parse_args()
assert (a.session/'session.json').is_file(),'not an initialized link session'
events=[json.loads(x) for x in (a.session/'events.jsonl').read_text().splitlines()] if (a.session/'events.jsonl').exists() else []
assert any(x['kind']==4 for x in events),'Windows worker has not connected'
old=json.loads((a.session/'job.json').read_text()) if (a.session/'job.json').exists() else None
assert old is None or any(x['kind']==3 and x['job']==old['id'] for x in events),'previous task is unfinished; no replacement'
script=a.script.read_text(encoding='ascii').replace('\r\n','\n').replace('\n','\r\n')
assert 0<len(script.encode('ascii'))<=4032,'script exceeds mailbox size'
job={'id':old['id']+1 if old else 1,'script':script}
tmp=a.session/'job.json.tmp';tmp.write_text(json.dumps(job));tmp.replace(a.session/'job.json')
print('Queued job',job['id'])
