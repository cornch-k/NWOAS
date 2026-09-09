#!/bin/bash
# S6-D4: capture first AFK ring geometry using the deferred-DCP bootstrap.
# HARDWARE-UNVERIFIED. Zero reboots/chainloads/kmutil; no DCP reset.
# Requires fresh boot of the exact deferred image. Allocates RTKit buffers
# using existing Python DART/ASC code. Stops before starting AFK queues.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-first-$(date '+%Y%m%d-%H%M%S').XXXXXX")
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
    class GeometryCaptured(Exception):
        pass
    def capture(self, ep, address, total):
        size, unknown = struct.unpack('<II', ep.iface.readmem(address, 8))
        print(f'[FACT] AFK first ring ep={ep.epnum:#x} total={total:#x} bufsz={size:#x} unk={unknown:#x}')
        if total >= size:
            print(f'[FACT] overhead={total-size:#x} candidate_block={(total-size)//3:#x} remainder={(total-size)%3}')
        raise GeometryCaptured()
    AFKRingBuf.__init__ = capture
    signal.alarm(180) # includes slow UART transfers for RTKit buffers/page tables
    try:
        # StandardASC.start uses a fixed 3s deadline; allow 30s for this
        # diagnostic's first handshake without changing baud or firmware.
        ASC.boot(dcp)
        dcp.mgmt.start()
        dcp.mgmt.wait_boot(30)
        print('[FACT] DCP management handshake complete')
        dcp.start_ep(0x23)
        while True:
            dcp.work()
    except GeometryCaptured:
        print('[FACT] Geometry captured; AFK queues were not started. Not display/Windows PASS.')
finally:
    signal.alarm(0)
    iface.dev.close()
PY
