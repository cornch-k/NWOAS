"""S243: host-side artifact manifest (create / load+verify). No guest or device access.

Manifest JSON (version 1):
  {"version": 1,
   "artifacts": [{"name": "X.sys", "path": "/abs/X.sys", "bytes": N, "sha256": "<hex>"}, ...]}

`load()` re-reads every file at launch and refuses to continue unless each path
is absolute, a regular non-symlink file, and both its size and SHA-256 equal the
manifest values (the manifest is the "expected" record; the file is the input).
"""
import hashlib
import json
import os
import stat
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # also works under python3 -I
from s243_channel import MAX_ARTIFACTS, valid_name

MAX_TOTAL = 1 << 30          # 1 GiB frozen in host (Mac) RAM, at most
HEX = set('0123456789abcdef')


def _check_sha(s):
    if not isinstance(s, str) or len(s) != 64 or not set(s) <= HEX:
        raise ValueError(f'bad sha256 {s!r} (need 64 lower-case hex)')
    return s


def _read_regular(path):
    p = Path(path)
    if not p.is_absolute():
        raise ValueError(f'path must be absolute: {path}')
    st = os.lstat(p)
    if stat.S_ISLNK(st.st_mode) or not stat.S_ISREG(st.st_mode):
        raise ValueError(f'not a regular non-symlink file: {path}')
    return p.read_bytes()


def entry_for(path, name=None):
    data = _read_regular(path)
    name = name or Path(path).name
    if not valid_name(name):
        raise ValueError(f'invalid artifact name {name!r}')
    return {'name': name, 'path': str(Path(path)), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}


def create(paths):
    """Build a manifest dict from files (directories expand to their regular files, sorted)."""
    files = []
    for p in paths:
        p = Path(p)
        if p.is_dir() and not p.is_symlink():
            files += sorted(x for x in p.iterdir() if x.is_file() and not x.is_symlink()
                            and not x.name.startswith('.'))
        else:
            files.append(p)
    return {'version': 1, 'artifacts': [entry_for(str(f.resolve())) for f in files]}


def load(manifest, max_total=MAX_TOTAL):
    """Verify a manifest dict against the files. Returns [(name, bytes)], [entry]."""
    if not isinstance(manifest, dict) or manifest.get('version') != 1:
        raise ValueError('manifest version must be 1')
    arts = manifest.get('artifacts')
    if not isinstance(arts, list) or not 1 <= len(arts) <= MAX_ARTIFACTS:
        raise ValueError('manifest needs 1..64 artifacts')
    out, meta, total = [], [], 0
    for e in arts:
        if set(e) != {'name', 'path', 'bytes', 'sha256'}:
            raise ValueError(f'manifest entry keys {sorted(e)}')
        if not valid_name(e['name']):
            raise ValueError(f'invalid artifact name {e["name"]!r}')
        want = _check_sha(e['sha256'])
        if not isinstance(e['bytes'], int) or e['bytes'] < 0:
            raise ValueError('bytes must be a non-negative int')
        total += e['bytes']
        if total > max_total:
            raise ValueError('manifest exceeds host RAM budget')
        data = _read_regular(e['path'])
        if len(data) != e['bytes']:
            raise ValueError(f'{e["name"]}: size {len(data)} != manifest {e["bytes"]}')
        got = hashlib.sha256(data).hexdigest()
        if got != want:
            raise ValueError(f'{e["name"]}: sha256 {got} != manifest {want}')
        out.append((e['name'], data))
        meta.append(dict(e))
    return out, meta


def from_env(env):
    """NWOAS_DELIVER_MANIFEST=<json>  or  NWOAS_DELIVER_FILE=<file> + NWOAS_DELIVER_SHA256=<hex>.
    Returns a manifest dict, or None when no delivery is requested."""
    mpath = env.get('NWOAS_DELIVER_MANIFEST', '')
    fpath = env.get('NWOAS_DELIVER_FILE', '')
    if mpath and fpath:
        raise ValueError('set either NWOAS_DELIVER_MANIFEST or NWOAS_DELIVER_FILE, not both')
    if mpath:
        return json.loads(_read_regular(mpath))
    if fpath:
        sha = _check_sha(env.get('NWOAS_DELIVER_SHA256', ''))
        data = _read_regular(fpath)
        return {'version': 1, 'artifacts': [{'name': Path(fpath).name, 'path': fpath,
                                             'bytes': len(data), 'sha256': sha}]}
    if env.get('NWOAS_DELIVER_SHA256'):
        raise ValueError('NWOAS_DELIVER_SHA256 given without NWOAS_DELIVER_FILE')
    return None


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit('usage: manifest.py OUT.json FILE_OR_DIR...')
    out = Path(sys.argv[1])
    if out.exists():
        sys.exit(f'refusing to overwrite {out}')
    m = create(sys.argv[2:])
    out.write_text(json.dumps(m, indent=1) + '\n')
    for e in m['artifacts']:
        print(e['bytes'], e['sha256'], e['name'])
