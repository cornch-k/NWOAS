#!/bin/bash
set -euo pipefail
ROOT=/Volumes/X31/NWOAS
HERE="$ROOT/nwoas_scripts/native-s229"
WT="$ROOT/m1n1-admin-s229"
[ "$(git -C "$WT" rev-parse HEAD)" = bddf7f06f033a7411834ac61381d8c997034f532 ]
python3 - "$HERE" "$WT" <<'CHECK'
import hashlib,json,sys
from pathlib import Path
h,w=map(Path,sys.argv[1:]);m=json.loads((h/'manifest.json').read_text())
for p,sha in m['sources'].items():assert hashlib.sha256((w/p).read_bytes()).hexdigest()==sha,p
CHECK
/opt/homebrew/opt/llvm/bin/clang --version | grep -q "clang version 20.1.8"
/opt/homebrew/opt/lld/bin/ld.lld --version | grep -q "LLD 20.1.8"
mkdir -p "$HERE/out"
export RUSTUP_TOOLCHAIN=1.88.0 CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2
unset M1N1_VERSION_TAG
cd "$WT"
nice -n 19 make TOOLCHAIN=/opt/homebrew/opt/llvm/bin/ LLDDIR=/opt/homebrew/opt/lld/bin/ USE_CLANG=1 EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50' -j2 > "$HERE/out/build.log" 2>&1
python3 - "$HERE" "$WT" <<'VERIFY'
import hashlib,json,sys
from pathlib import Path
h,w=map(Path,sys.argv[1:]);p=h/'manifest.json';m=json.loads(p.read_text());data=(w/'build/m1n1.bin').read_bytes();sha=hashlib.sha256(data).hexdigest()
if 'hv_sha256' in m and m['hv_sha256']!=sha:raise SystemExit('Recorded candidate differs; refusing replacement')
m['hv_sha256']=sha;m['hv_bytes']=len(data);p.write_text(json.dumps(m,indent=2)+'\n')
(h/'out/m1n1-s229-admin.bin').write_bytes(data)
print('S229 built',len(data),sha)
VERIFY
