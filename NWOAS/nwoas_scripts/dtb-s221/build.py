#!/usr/bin/env python3
"""Rebuild the static J274 FDT from recovered DTS, verify exact baseline bytes.
No device, live ADT, installed firmware or original artifact is accessed.
"""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile
D=Path(__file__).resolve().parent
expected='ecc93b24740d80007d31986a43983c36eef47dd8b0b105b663c25a7b51190f76'
dtc=os.environ.get('NWOAS_DTC','/opt/homebrew/bin/dtc')
version=subprocess.check_output([dtc,'--version'],text=True).strip()
assert version=='Version: DTC 1.8.1',version
out=D/'out';out.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='dtb-s221-',dir=out) as tmp:
 p=Path(tmp)/'apple-j274-padded.dtb'
 r=subprocess.run([dtc,'-I','dts','-O','dtb','-S','65536','-o',str(p),str(D/'recovered-j274.dts')],capture_output=True,text=True,check=True)
 b=p.read_bytes();actual=hashlib.sha256(b).hexdigest();assert len(b)==65536 and actual==expected,(len(b),actual)
 dest=out/p.name
 if dest.exists():assert dest.read_bytes()==b,'existing output differs; preserved'
 p.replace(dest)
(out/'build-warnings.log').write_text(r.stderr)
j={'status':'PASS','dtc':version,'bytes':len(b),'sha256':actual,'source_sha256':hashlib.sha256((D/'recovered-j274.dts').read_bytes()).hexdigest(),'hardware_changed':False,'upstream_source_revision':'unknown; recovered DTS reproduces existing static FDT exactly'}
(D/'reproduce-result.json').write_text(json.dumps(j,indent=2)+'\n');print(json.dumps(j))
