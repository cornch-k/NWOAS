#!/bin/bash
# S6-D3: read DCP DART fault latch and current segment translations.
# No reboot/reset, no target memory/MMIO writes, no fault acknowledgement.
# A stale error without FLAG is not a current fault. No firmware change.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-mapping-$(date '+%Y%m%d-%H%M%S').XXXXXX")
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
from pathlib import Path
from types import SimpleNamespace
import signal, struct, subprocess
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.adt import load_adt
from m1n1.hw.dart import DART
owners = subprocess.run(['/usr/sbin/lsof', '-t', '/dev/cu.debug-console'], capture_output=True)
assert not owners.stdout.strip() and not owners.stderr.strip() and owners.returncode in (0,1)
def deadline(signum, frame):
    raise TimeoutError('120-second read-only diagnostic deadline')
signal.signal(signal.SIGALRM, deadline)
signal.alarm(120)
iface = UartInterface('/dev/cu.debug-console:115200')
iface.dev.timeout = 5
iface.dev.write_timeout = 3
try:
    iface.cmd(iface.REQ_NOP)
    iface.reply(iface.REQ_NOP)
    p = M1N1Proxy(iface)
    assert p.get_base() == 0x802e28000, 'Target changed since ADT capture'
    adt = load_adt(Path('/Volumes/X31/NWOAS/nwoas_scripts/logs/tahoe-dcp-late-20260906-222055.K2gj5O.adt').read_bytes())
    assert adt.model == 'Macmini9,1'
    def read(addr, width):
        return {32: p.read32, 64: p.read64}[width](addr)
    def forbidden_write(*args, **kwargs):
        raise RuntimeError('Read-only diagnostic forbids register writes')
    u = SimpleNamespace(adt=adt, iface=iface, proxy=p, read=read, write=forbidden_write)
    dart = DART.from_adt(u, '/arm-io/dart-dcp')
    def show_latch():
        regs = dart.dart.regs
        print('[FACT] DART latch (not cleared):', regs.ERROR.reg)
        print('[FACT] DART fault address:', hex((regs.ERROR_ADDR_HI.val << 32) | regs.ERROR_ADDR_LO.val))
    show_latch()
    sid = adt['/arm-io/dart-dcp'][0].reg
    print('[FACT] stream:', sid)
    print('[FACT] TCR:', dart.dart.regs.TCR[sid].reg)
    for i in range(4):
        print('[FACT] TTBR:', i, dart.dart.regs.TTBR[sid,i].reg)
    nub = adt['/arm-io/dcp'][0]
    mask = nub.asc_dram_mask
    for name, fields in zip(nub.segment_names.split(';'), struct.iter_unpack('<QQQII', nub.segment_ranges)):
        phys, original, remap, size, flags = fields
        for label, addr in [('original', original), ('remap', remap & ~mask)]:
            if addr >= (1 << 38):
                continue
            print(f'[FACT] {name} {label}={addr:#x} expected_phys={phys:#x} size={size:#x}')
            print('[FACT] translation:', dart.iotranslate(sid, addr, 4))
    show_latch()
finally:
    signal.alarm(0)
    iface.dev.close()
PY
