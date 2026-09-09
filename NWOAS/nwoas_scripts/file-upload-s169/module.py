"""Register one fixed installation-source backup before guest boot."""
import os,sys,json
from pathlib import Path
if os.environ.get('NWOAS_BACKUP_INSTALL_SOURCE')!='1':
    raise RuntimeError('S169 requires explicit installation-source backup opt-in')
root=Path('/Volumes/X31/NWOAS/nwoas_scripts')
sys.path.insert(0,str(root/'file-upload-s169'))
from receiver import Receiver
from upload_adapter import UploadLink
# No guest-supplied path. X31 experiment output only, never an existing file.
dest=root/'file-export-s168/verified-backup/install.swm'
dest.parent.mkdir(parents=True,exist_ok=True)
if dest.exists() or dest.is_symlink() or dest.with_name(dest.name+'.part').exists():
    raise RuntimeError('S169 backup or partial exists; refuse overwrite')
ctrl=hv._nwoas_nvme[0]
if ctrl.ns.link is not hv._nwoas_link:raise RuntimeError('S169 unexpected namespace binding')
rx=Receiver(hv._nwoas_link.token,3987720636,'8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c',dest)
wrapped=UploadLink(hv._nwoas_link,rx);ctrl.ns.link=wrapped;hv._nwoas_link=wrapped
hv.log('[S169] fixed installation backup channel registered, 64KiB write160..175 ACK176; no SSD guard or capacity changes')
Path(os.environ['NWOAS_LINK_DIR'],'upload.json').write_text(json.dumps({'destination':str(dest),'bytes':rx.size,'sha256':rx.digest.hex()})+'\n')
