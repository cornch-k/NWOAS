#!/bin/bash
# S6-D2: check for a delayed DCP reply after the captured HELLO timeout.
# [DESIGN] Observe mailbox control only for 30 seconds at 115200.
# Pending mailbox data is NOT identified as HELLO without reading a message.
# Safety: zero reboot/reset/chainload/guest, no writes to target memory/MMIO,
# no dequeue of DCP messages, no passwords. Read-only proxy operations only.
set -eu
ROOT=/Volumes/X31/NWOAS
export NWOAS_DCP_ACTION="${1:-observe}"
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-late-$(date '+%Y%m%d-%H%M%S').XXXXXX")
echo "Log: $LOG"
export NWOAS_OBSERVATION_LOG="$LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
from pathlib import Path
import signal
import subprocess
import time
import os
import struct
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.adt import load_adt

owners = subprocess.run(['/usr/sbin/lsof', '-t', '/dev/cu.debug-console'], capture_output=True)
if owners.stdout.strip() or owners.stderr.strip() or owners.returncode not in (0, 1):
    raise SystemExit('STOP: serial ownership uncertain or busy')
adt = load_adt(Path('/Volumes/X31/NWOAS/experiments/tahoe-afk-20260906/current.adt').read_bytes())
assert adt.model == 'Macmini9,1'
cpu = adt['/arm-io/dcp'].get_reg(0)[0]
def deadline(signum, frame):
    raise TimeoutError('45-second observation deadline')
signal.signal(signal.SIGALRM, deadline)
signal.alarm(45)
iface = UartInterface('/dev/cu.debug-console:115200')
iface.dev.timeout = 3
iface.dev.write_timeout = 3
try:
    t = time.monotonic()
    iface.cmd(iface.REQ_NOP)
    iface.reply(iface.REQ_NOP)
    print(f'[FACT] NOP latency={time.monotonic()-t:.3f}s at 115200')
    p = M1N1Proxy(iface)
    print(f'[FACT] m1n1 base={p.get_base():#x}, DCP registers={cpu:#x}')
    if os.environ['NWOAS_DCP_ACTION'] == 'snapshot':
        from m1n1.tgtypes import BootArgs_r1, BootArgs_r2, BootArgs_r3
        ba_addr, rev = p.get_bootargs_rev()
        assert rev in (1, 2, 3)
        ba = iface.readstruct(ba_addr, {1: BootArgs_r1, 2: BootArgs_r2, 3: BootArgs_r3}[rev])
        addr = (ba.devtree - ba.virt_base + ba.phys_base) & ((1 << 64)-1)
        assert 0 < ba.devtree_size <= 0x100000
        print(f'[FACT] Fetching current ADT: {ba.devtree_size} bytes at 115200; allow transfer time')
        signal.alarm(120)
        raw = iface.readmem(addr, ba.devtree_size)
        destination = Path(os.environ['NWOAS_OBSERVATION_LOG'] + '.adt')
        destination.write_bytes(raw)
        current = load_adt(raw)
        assert current.model == 'Macmini9,1'
        print('[FACT] Snapshot:', destination)
        for node in current['/arm-io/dcp']:
            if hasattr(node, 'segment_ranges'):
                print('[FACT] DCP segments:', node.segment_names)
                for idx, fields in enumerate(struct.iter_unpack('<QQQII', node.segment_ranges)):
                    print(idx, *(hex(value) for value in fields))
        raise SystemExit(0)
    assert os.environ['NWOAS_DCP_ACTION'] == 'observe'
    print(f'[FACT] DCP CPU control={p.read32(cpu + 0x44):#x}')
    start = time.monotonic()
    pending = 0
    samples = 0
    previous = None
    while time.monotonic() - start < 30:
        controls = (p.read32(cpu + 0x8110), p.read32(cpu + 0x8114))
        samples += 1
        if not controls[1] & (1 << 17):
            pending += 1
        if controls != previous:
            print(f'[FACT] t={time.monotonic()-start:.3f}s A2I={controls[0]:#x} I2A={controls[1]:#x}')
            previous = controls
        time.sleep(0.5)
    print(f'[FACT] observation={time.monotonic()-start:.2f}s samples={samples} pending_samples={pending}')
    print('[LIMIT] Observation after earlier timeout, not a timed fresh boot experiment. No message dequeued.')
finally:
    signal.alarm(0)
    iface.dev.close()
PY
