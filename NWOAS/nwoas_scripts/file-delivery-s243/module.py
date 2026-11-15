"""S243: opt-in preboot artifact registration; never reload into a running guest.

Loaded by run_guest.py -m right after memory-s180/link_module.py. Without
NWOAS_DELIVER_MANIFEST / NWOAS_DELIVER_FILE it does nothing. With them it:
  1. verifies every artifact (absolute regular file, size + SHA-256 = manifest),
  2. verifies the pinned guest client NWGET243.EXE and appends it to the still
     pristine NS2 RAM FAT image (D:), and
  3. wraps hv._nwoas_link in ArtifactLink: only LBA 127 (1-block request write)
     and reads overlapping LBA 129..144 change; NS1/SSD is never referenced.
"""
import hashlib
import json
import os
import sys
from pathlib import Path

_dir243 = Path('/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/file-delivery-s243')
sys.path.insert(0, str(_dir243))
import s243_manifest as _manifest243         # noqa: E402

_m243 = _manifest243.from_env(os.environ)
if _m243 is None:
    hv.log('[S243] no artifact delivery requested (NWOAS_DELIVER_MANIFEST/FILE unset); link unchanged')
else:
    from s243_adapter import ArtifactLink    # noqa: E402
    from s243_fat import fat_append    # noqa: E402
    _CLIENT243_SHA = 'fc457722e1aa8e384385f6118a3719c86268cfb339497ddff218b0d113222f5b'
    _client243 = (_dir243 / 'client/NWGET243.EXE').read_bytes()
    if hashlib.sha256(_client243).hexdigest() != _CLIENT243_SHA:
        raise RuntimeError('S243 client NWGET243.EXE SHA256 mismatch')
    _arts243, _meta243 = _manifest243.load(_m243)
    _ctrl243 = hv._nwoas_nvme[0]
    _link243 = hv._nwoas_link
    if _ctrl243.ns.link is not _link243 or isinstance(_link243, ArtifactLink):
        raise RuntimeError('S243 unexpected namespace binding')
    if (_link243.block_count != 8448 or _link243.connected or _link243.job is not None
            or _link243.ack != 0):
        raise RuntimeError('S243 link is not in preboot state')
    _link243.image[:] = fat_append(bytes(_link243.image), 'NWGET243.EXE', _client243)
    _wrapped243 = ArtifactLink(_link243, _arts243)
    _ctrl243.ns.link = _wrapped243
    hv._nwoas_link = _wrapped243
    for _e in _wrapped243.channel.describe():
        hv.log(f'[S243] artifact {_e["index"]} {_e["name"]} {_e["bytes"]} bytes SHA256={_e["sha256"]}')
    hv.log(f'[S243] {len(_arts243)} frozen artifact(s); NWGET243.EXE appended to NS2 FAT; '
           'request LBA127, response LBA129-144; NS1, capacity, PORT128 unchanged')
    Path(os.environ['NWOAS_LINK_DIR'], 'artifact.json').write_text(json.dumps({
        'artifacts': _wrapped243.channel.describe(), 'client_sha256': _CLIENT243_SHA,
        'request_lba': 127, 'response_lba': [129, 144]}, indent=1) + '\n')
    del _arts243, _client243
