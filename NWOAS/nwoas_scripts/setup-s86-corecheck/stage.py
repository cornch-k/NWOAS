#!/usr/bin/env python3
"""Stage exact S86 core-count experiment onto verified WINARM2, with originals.
No format/partition/image installation. Replacements are fully written and
readback verified before atomic rename. Rerunning on changed media is refused.
"""
from pathlib import Path
import hashlib,json,plistlib,subprocess,shutil,os,datetime
r=Path(__file__).resolve().parent;v=Path('/Volumes/WINARM2')
i=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',str(v)]))
assert i['VolumeUUID']=='8AA1ED40-57BA-3284-9023-B310B595EC94'
assert i['MountPoint']==str(v) and i['Internal'] is False and i['BusProtocol']=='USB'
assert i['FilesystemType']=='msdos' and 12_000_000_000<i['TotalSize']<20_000_000_000
assert (v/'NWOAS-S84.TAG').read_text().strip()=='NWOAS-S84-WINARM2-20260907'
m=json.loads((r/'patch-manifest.json').read_text())
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
pairs=[('boot.wim',m['original/boot.wim']['sha256'],m['patched/boot.wim']['sha256']),('winsetup.dll',m['original_sha256'],m['patched_sha256'])]
for name,before,after in pairs:
 dest=v/'sources'/name
 assert not dest.is_symlink() and not dest.parent.is_symlink()
 assert sha(dest)==before and sha(r/'patched'/name)==after
backup=v/('NWOAS-S86-BACKUP-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));backup.mkdir()
for name,before,after in pairs:
 dest=v/'sources'/name;shutil.copy2(dest,backup/name);assert sha(backup/name)==before
 tmp=dest.with_name(name+'.nwoas-s86-tmp')
 with (r/'patched'/name).open('rb') as src,tmp.open('xb') as out:
  shutil.copyfileobj(src,out,1024*1024);out.flush();os.fsync(out.fileno())
 assert sha(tmp)==after
for name,before,after in pairs:
 dest=v/'sources'/name;os.replace(dest.with_name(name+'.nwoas-s86-tmp'),dest);assert sha(dest)==after
(v/'NWOAS-S86-PATCH.json').write_text(json.dumps(m,indent=2)+'\n')
subprocess.run(['sync'],check=True)
report={'status':'STAGED_NOT_HARDWARE_VERIFIED','device':i['DeviceIdentifier'],'backup':str(backup),'manifest':m}
(r/'staging.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: S86 USB replacements readback verified. Backup:',backup)
