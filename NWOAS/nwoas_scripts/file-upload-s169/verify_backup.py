"""Run on host after END success; fsync and rehash outside guest rendezvous."""
import json,hashlib,os
from pathlib import Path
r=Path(__file__).resolve().parents[1];dest=r/'file-export-s168/verified-backup/install.swm'
m=json.loads((r/'file-export-s168/sources.json').read_text())[0]
if dest.is_symlink() or dest.stat().st_size!=m['bytes']:raise SystemExit('Backup path/size mismatch')
h=hashlib.sha256()
with dest.open('rb') as f:
 os.fsync(f.fileno())
 for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
if h.hexdigest()!=m['sha256']:raise SystemExit('Backup SHA256 mismatch')
result={'pass':True,'backup':str(dest),'source':m,'host_fsync':True}
(dest.parent/'install.swm.verified.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
