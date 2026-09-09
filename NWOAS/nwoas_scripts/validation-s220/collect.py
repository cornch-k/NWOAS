"""Collect bounded Windows test outputs; no live device access or raw boot logs."""
import argparse,json,re,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('kind',choices=['integration','active']);p.add_argument('link',type=Path);p.add_argument('job',type=int);p.add_argument('output',type=Path);a=p.parse_args()
events=[json.loads(x) for x in (a.link/'events.jsonl').read_text().splitlines() if x]
assert any(x.get('job')==a.job and x.get('kind')==3 and x.get('exit')==0 for x in events),'no acknowledged exit0'
t=(a.link/f'job-{a.job}.log').read_text(errors='replace')
r={'status':'PASS','session':str(a.link),'job':a.job}
def num(pattern):
 m=re.search(pattern,t);assert m,pattern;return int(m.group(1))
if a.kind=='integration':
 r.update(cpu_count=num(r'NumberOfLogicalProcessors\s*:\s*(\d+)'),memory_bytes=num(r'TotalPhysicalMemory\s*:\s*(\d+)'),cpu_us=num(r'S140 CPU PASS active=8 threads=8 elapsed_us=(\d+)'),read64m_us=num(r'S140 DISK PASS active=8 bytes=67108864 elapsed_us=(\d+) failed=0'),write256m_us=num(r'write_us=(\d+)'),read256m_us=num(r'read_us=(\d+)'),all_words_verified=num(r'words_verified=(\d+)'))
 assert r['cpu_count']==8 and r['memory_bytes']>14_000_000_000 and r['all_words_verified']==33_554_432 and 'S195 PASS exit=0' in t
 r['native_p12']=json.loads((a.link/'native-p12.json').read_text());assert r['native_p12']['p12_selected']
 r['qualification']='Short integration only; see separate active/soak results'
else:
 vals=[int(x) for x in re.findall(r'S140 DISK PASS active=8 bytes=67108864 elapsed_us=(\d+) failed=0 checksum=2755686512017749371',t)]
 samples=re.findall(r'S163_ACTIVE (\d+) elapsed_ms=(\d+)',t)
 end=re.search(r'S163_ACTIVE_PASS samples=(\d+) elapsed_ms=(\d+)',t)
 assert samples and end and len(vals)==int(samples[-1][0])==int(end[1]) and int(end[2])>=300000
 assert [int(x[0]) for x in samples]==list(range(1,len(samples)+1))
 r.update(samples=len(vals),elapsed_ms=int(end[2]),disk_us={'min':min(vals),'median':statistics.median(vals),'max':max(vals)},note='Logical 64MiB reads on a compressed filesystem; not raw SSD throughput')
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
