"""Copy explicit public source/evidence updates; never stage, commit or push."""
from pathlib import Path
import json,hashlib
ROOT=Path('/Volumes/X31/NWOAS');S=ROOT/'nwoas_scripts'
D=Path('/Volumes/X31/NWOAS-publish-20260907/NWOAS/nwoas_scripts')
folders=['loader-s197','loader-s199','firmware-s204','rebuild-s205','native-s207','nvme-s208','native-s209','firmware-s210','nvme-s211','loader-s213','nvme-s214','loader-s215','firmware-s216','publish-s217','bench-s218','native-s219','validation-s220','dtb-s221','diagnostics-s222','loader-s223','loader-s203','nvme-s202','storage-s195']
excluded={'out','build','source-before','source-candidate','s131-build','__pycache__','logs','verified-backup','certificates','symbols','venv','.git'}
allowed={'.py','.c','.h','.md','.json','.sh','.patch','.diff','.cmd','.ps1','.inc','.inf','.txt','.def','.asl','.aslc','.dsc','.dts'}
block={'native-s219/commit-api.json','publish-s217/prepared-delta.json','publish-s217/stage-paths.txt'}
paths=set()
for folder in folders:
 paths.update(p for p in (S/folder).rglob('*') if p.is_file())
for stage in [203,204,206,211,212,214,215,216,223]:paths.update(S.glob(f'*s{stage}*guest-test.sh'))
paths.add(S/'smp-s133/pmgr_gate.py')
paths.add(S/'nvme-s163/assess_soak.py')
records=[]
for p in sorted(paths):
 rel=str(p.relative_to(S))
 if rel.startswith('dtb-s221/') and rel.removeprefix('dtb-s221/') not in {'README.md','NOTICE.md','build.py','recovered-j274.dts','roundtrip.json','reproduce-result.json','payload-assembly.json','claude-review.md','source-comparisons.json'}:continue
 if rel in block or p.is_symlink() or any(x in excluded or x.startswith('.env') for x in p.relative_to(S).parts):continue
 if p.suffix not in allowed or p.name=='s131_builder.py' or p.stat().st_size>1000000:continue
 data=p.read_bytes()
 try:data.decode('utf-8')
 except UnicodeDecodeError:raise SystemExit('Non-text candidate '+rel)
 dest=D/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data);dest.chmod(p.stat().st_mode & 0o777)
 records.append({'path':rel,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
(S/'publish-s217/prepared-delta.json').write_text(json.dumps(records,indent=2)+'\n')
print('Prepared',len(records),'text files; no staging or push')
