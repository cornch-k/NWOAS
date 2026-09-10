#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
python3 - <<'PY'
from pathlib import Path
w=Path('/Volumes/X31/NWOAS/m1n1-admin-s229');t=(w/'src/hv_vm.c').read_text()
a=t.index('static struct {',t.index('invalid guest NVMe CQE size'));b=t.index('static u64 nwoas_probe_at_el1',a)
Path('out/state_actual.inc').write_text(t[a:b])
a=t.index('u64 nwoas_nvme_fastpath_control(u64 action');b=t.index('static bool nwoas_nvme_fastpath_mmio',a);Path('out/control_actual.inc').write_text(t[a:b])
a=b;b=t.index('// Cache-maintain',a);Path('out/mmio_actual.inc').write_text(t[a:b])
t=(w/'src/nvme.c').read_text();a=t.index('u64 nvme_guest_page_pa(u64 gpa)');b=t.index('/* An unaligned PRP1',a);Path('out/page_actual.inc').write_text(t[a:b])
PY
/usr/bin/clang -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all -Iout -I. -I/Volumes/X31/NWOAS/m1n1-admin-s229/src -I../native-s209 -I../native-s225 -I../native-s226 -I../native-s227 -I../native-s228 test_adapter.c ../native-s228/frontend.c ../native-s209/model.c ../native-s227/admin_ring.c ../native-s226/admin_state.c ../native-s225/admin_payload.c -o out/test-adapter
./out/test-adapter
