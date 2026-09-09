"""Opt-in preboot artifact registration; never reload into a running guest."""
import os,sys,hashlib,json
from pathlib import Path
if os.environ.get('NWOAS_DELIVER_CINEBENCH')!='1':
    raise RuntimeError('S168 requires explicit artifact-delivery opt-in')
root=Path('/Volumes/X31/NWOAS/nwoas_scripts')
sys.path.insert(0,str(root/'file-delivery-s168'))
from adapter import ArtifactLink
artifact=root/'benchmarks/cinebench-2026/Cinebench2026_win_arm64.zip'
if artifact.is_symlink() or artifact.stat().st_size!=783373346:
    raise RuntimeError('S168 artifact path/size mismatch')
blob=artifact.read_bytes()
expected='cb6c765f80d53e1fe702de145b6da1c67af5b37d5a399c8f3396a7ecfed78159'
if hashlib.sha256(blob).hexdigest()!=expected:
    raise RuntimeError('S168 artifact SHA256 mismatch')
ctrl=hv._nwoas_nvme[0]
if ctrl.ns.link is not hv._nwoas_link:
    raise RuntimeError('S168 unexpected namespace binding')
wrapped=ArtifactLink(hv._nwoas_link,blob)
ctrl.ns.link=wrapped
hv._nwoas_link=wrapped
hv.log(f'[S168] immutable official ARM64 Cinebench artifact {len(blob)} bytes SHA256={expected}; NS1, capacity, FAT, PORT128 unchanged')
Path(os.environ['NWOAS_LINK_DIR'],'artifact.json').write_text(json.dumps({'bytes':len(blob),'sha256':expected,'request_lba':127,'response_lba':[129,144]})+'\n')
del blob
