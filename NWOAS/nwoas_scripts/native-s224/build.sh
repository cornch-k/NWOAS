#!/bin/bash
set -euo pipefail
ROOT=/Volumes/X31/NWOAS
HERE="$ROOT/nwoas_scripts/native-s224"
WT="$ROOT/m1n1-read-s224"
[ "$(git -C "$WT" rev-parse HEAD)" = bddf7f06f033a7411834ac61381d8c997034f532 ]
cmp "$HERE/read_mirror.h" "$WT/src/nwoas_read_mirror.h"
/opt/homebrew/opt/llvm/bin/clang --version | grep -q 'clang version 20.1.8'
/opt/homebrew/opt/lld/bin/ld.lld --version | grep -q 'LLD 20.1.8'
python3 - "$HERE" "$WT" <<'CHECK'
import hashlib,json,sys
from pathlib import Path
h,w=map(Path,sys.argv[1:]);m=json.loads((h/'manifest.json').read_text())
assert hashlib.sha256((w/'src/hv_vm.c').read_bytes()).hexdigest()==m['hv_vm_sha256']
assert hashlib.sha256((h/'read_mirror.h').read_bytes()).hexdigest()==m['projection_sha256']
CHECK

mkdir -p "$HERE/out"
export RUSTUP_TOOLCHAIN=1.88.0 CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2
unset M1N1_VERSION_TAG
cd "$WT"
nice -n 19 make TOOLCHAIN=/opt/homebrew/opt/llvm/bin/ LLDDIR=/opt/homebrew/opt/lld/bin/ USE_CLANG=1 EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50' -j2 > "$HERE/out/build.log" 2>&1
python3 - "$HERE" "$WT" <<'VERIFY'
import hashlib,json,sys
from pathlib import Path
h,w=map(Path,sys.argv[1:]);path=h/'manifest.json';m=json.loads(path.read_text());data=(w/'build/m1n1.bin').read_bytes();sha=hashlib.sha256(data).hexdigest()
if 'hv_sha256' in m and m['hv_sha256']!=sha:raise SystemExit('Rebuild differs from recorded candidate; refusing to replace validated artifact')
m['hv_sha256']=sha;m['hv_bytes']=len(data);path.write_text(json.dumps(m,indent=2)+'\n')
VERIFY
cp build/m1n1.bin "$HERE/out/m1n1-s224-read-mirror.bin"
cp build/m1n1-raw.elf "$HERE/out/m1n1-s224-read-mirror-raw.elf"
shasum -a 256 "$HERE/out/m1n1-s224-read-mirror.bin"
