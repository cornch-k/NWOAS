#!/bin/bash
# S6-D5: enumerate HDMI display modes using 128-byte AFK ring pointers.
# HARDWARE-UNVERIFIED. Zero reboots/chainloads/kmutil; no DCP reset.
# Requires fresh boot of the exact deferred image. Allocates RTKit buffers
# using existing Python DART/ASC code. Queries modes only, then quiesces DCP. No modeset or Windows guest.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-modes-$(date '+%Y%m%d-%H%M%S').XXXXXX")
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
from pathlib import Path
import hashlib, signal, struct, subprocess, time
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils
from m1n1.hw.dart import DART
from m1n1.hw.asc import ASC
from m1n1.fw.dcp.iboot import DCPIBootClient
from m1n1.fw.afk.rbep import AFKRingBuf

root = Path('/Volumes/X31/NWOAS')
binary = (root/'experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin').read_bytes()
assert hashlib.md5(binary).hexdigest() == '7bec0225edc101857be3b7f195dd7357'
marker = b'NWOAS-DCP-DEFER: automatic display init deferred to host'
offset = binary.index(marker)
owners = subprocess.run(['/usr/sbin/lsof','-t','/dev/cu.debug-console'],capture_output=True)
assert owners.returncode in (0,1) and not owners.stdout.strip() and not owners.stderr.strip()
def deadline(signum, frame):
    raise TimeoutError('first-handshake diagnostic deadline')
signal.signal(signal.SIGALRM, deadline)
signal.alarm(120)
iface = UartInterface('/dev/cu.debug-console:115200')
iface.dev.timeout = 5
iface.dev.write_timeout = 3
try:
    iface.cmd(iface.REQ_NOP)
    iface.reply(iface.REQ_NOP)
    p = M1N1Proxy(iface)
    base = p.get_base()
    if iface.readmem(base + offset, len(marker)) != marker:
        raise SystemExit('STOP: deferred bootstrap not running; no DCP operations performed')
    u = ProxyUtils(p)
    assert u.adt.model == 'Macmini9,1'
    assert b'mBoot-18000.121.3' in bytes(u.adt['/chosen'].firmware_version)
    dart = DART.from_adt(u, '/arm-io/dart-dcp')
    dcp = DCPIBootClient(u, u.adt['/arm-io/dcp'].get_reg(0)[0], dart)
    dcp.dva_offset = u.adt['/arm-io/dcp'][0].asc_dram_mask
    signal.alarm(180) # includes slow UART transfers for RTKit buffers/page tables
    ASC.boot(dcp)
    dcp.mgmt.start()
    dcp.mgmt.wait_boot(30)
    print('[FACT] DCP management handshake complete')
    dcp.start_ep(0x23)
    dcp.iboot.wait_for('disp0')
    print('[FACT] AFK sizes:', dcp.iboot.txq.block_size, dcp.iboot.rxq.block_size)
    status = dcp.iboot.disp0.getModeCount()
    print('[FACT] HDMI status (HPD, timing count, color count):', status)
    if status[0]:
        print('[FACT] Timing modes:', dcp.iboot.disp0.getTimingModes())
        print('[FACT] Color modes:', dcp.iboot.disp0.getColorModes())
    print('[FACT] Mode query completed; no modeset or Windows guest')
    dcp.stop(0x10)
    print('[FACT] DCP quiesced')

finally:
    signal.alarm(0)
    iface.dev.close()
PY
