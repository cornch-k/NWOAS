#!/usr/bin/env python3
"""Validate a finished raw binary job without printing private payload bytes."""
import argparse, hashlib, json, os
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('session',type=Path);p.add_argument('job',type=int)
p.add_argument('--source',choices=['test','install.swm','install2.swm'],required=True)
p.add_argument('--backup-dir',type=Path)
a=p.parse_args();root=Path(__file__).resolve().parent
if a.source=='test':
    expected={'name':'binary-test','bytes':1048613,'sha256':'026ceec48d4f9090d485c816f48395cc854d8190dfd9aef284901dd867e928e9'}
else:
    expected=next(s for s in json.loads((root/'sources.json').read_text()) if s['name']==a.source)
events=[json.loads(l) for l in (a.session/'events.jsonl').read_text().splitlines()]
ends=[e for e in events if e.get('job')==a.job and e.get('kind')==3]
if len(ends)!=1 or ends[0].get('exit')!=0:raise SystemExit('Not a uniquely completed exit-zero export')
f=a.session/f'job-{a.job}.log'
if f.is_symlink() or not f.is_file():raise SystemExit('Refuse unexpected output path')
size=f.stat().st_size
if size!=expected['bytes']:raise SystemExit(f'Length mismatch: {size} != {expected["bytes"]}')
h=hashlib.sha256()
with f.open('rb') as s:
    for b in iter(lambda:s.read(1024*1024),b''):h.update(b)
if h.hexdigest()!=expected['sha256']:raise SystemExit('SHA256 mismatch')
result={'pass':True,'session':str(a.session),'job':a.job,'source':expected,'output':str(f.resolve())}
if a.backup_dir:
    if a.source=='test':raise SystemExit('Test is not an installation backup')
    destdir=a.backup_dir.resolve();destdir.mkdir(parents=True,exist_ok=True)
    # Same-volume hard link retains the verified bytes independently of log pathname.
    dest=destdir/expected['name']
    if dest.exists():raise SystemExit('Backup already exists; refuse overwrite')
    os.link(f,dest)
    result['backup']=str(dest)
    (destdir/(expected['name']+'.verified.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
