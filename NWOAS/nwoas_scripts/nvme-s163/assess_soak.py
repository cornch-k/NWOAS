"""Summarize a completed read-mostly comparison, without touching hardware."""
import argparse
import datetime as dt
import json
import re
import statistics
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('session', type=Path)
parser.add_argument('job', type=int)
parser.add_argument('--samples', type=int, default=31)
args = parser.parse_args()
log = args.session / f'job-{args.job}.log'
body = log.read_text(errors='replace')
events = [json.loads(line) for line in (args.session/'events.jsonl').read_text().splitlines()]
parts = re.split(r'S160_SAMPLE (\d+) ([^\s]+)', body)
samples = []
for offset in range(1, len(parts), 3):
    number, stamp, output = parts[offset:offset+3]
    cpus = re.findall(r'S140 CPU PASS active=(\d+) threads=(\d+) elapsed_us=(\d+) checksum=(\d+)', output)
    disks = re.findall(r'S140 DISK PASS active=(\d+) bytes=(\d+) elapsed_us=(\d+) failed=(\d+) checksum=(\d+)', output)
    valid = (len(cpus) == len(disks) == 1 and cpus[0][0:2] == ('8', '8') and
             cpus[0][3] == '14964600543233568963' and
             disks[0][0:2] == ('8', '67108864') and disks[0][3:] == ('0', '2755686512017749371'))
    samples.append(dict(number=int(number), guest_time=stamp, valid=valid,
                        cpu_us=int(cpus[0][2]) if len(cpus)==1 else None,
                        disk_us=int(disks[0][2]) if len(disks)==1 else None))
complete = any(e.get('kind')==3 and e.get('job')==args.job and e.get('exit')==0 for e in events)
passed = (complete and 'THIRTY MINUTE READ-MOSTLY SOAK PASS' in body and
          [s['number'] for s in samples] == list(range(1,args.samples+1)) and
          all(s['valid'] and s['cpu_us']>0 and s['disk_us']>0 for s in samples))
result = dict(session=str(args.session), job=args.job, pass_=passed,
              completed_exit0=complete, samples=samples,
              note='Repeated read-only 64MiB test and bounded CPU work; not a general SSD benchmark or lifetime stability proof.')
for kind in ('cpu','disk'):
    values = [s[kind+'_us'] for s in samples if s['valid']]
    if values:
        result[kind+'_us'] = dict(min=min(values), median=statistics.median(values), max=max(values))
if len(samples)>1:
    result['guest_timestamp_span_s'] = (dt.datetime.fromisoformat(samples[-1]['guest_time'])-
                                         dt.datetime.fromisoformat(samples[0]['guest_time'])).total_seconds()
if (args.session/'job.json').is_file():
    current=json.loads((args.session/'job.json').read_text())
    if current.get('id')==args.job:
        result['host_queue_to_last_log_write_s'] = log.stat().st_mtime-(args.session/'job.json').stat().st_mtime
print(json.dumps(result, indent=2))
raise SystemExit(0 if passed else 1)
