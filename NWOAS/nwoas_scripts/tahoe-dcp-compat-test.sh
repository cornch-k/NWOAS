#!/bin/bash
# S6-D6: one legacy read-only mode-count request after a decoded new announcement.
# HARDWARE-UNVERIFIED. Zero reboots/chainloads/kmutil; no DCP reset.
# Requires fresh boot of the exact deferred image. Allocates RTKit buffers
# using existing Python DART/ASC code. Queries modes only, then quiesces DCP. No modeset or Windows guest.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-compat-$(date '+%Y%m%d-%H%M%S').XXXXXX")
echo "Log: $LOG"
export NWOAS_PROTOCOL_LOG="$LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
from pathlib import Path
import os, sys
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
    sys.path.insert(0, str(root/'nwoas_scripts'))
    from decode_tahoe_announce import decode
    from m1n1.fw.afk.epic import EPICEndpoint
    old_handler = EPICEndpoint.handle_ipc
    observed = [0]
    def handle(self, data):
        observed[0] += 1
        frame = struct.pack('<4sI', b'IOP ', len(data)-8) + data
        destination = Path(os.environ['NWOAS_PROTOCOL_LOG'] + f'.msg{observed[0]}.bin')
        destination.write_bytes(frame)
        print('[FACT] Raw IPC saved:', destination, 'prefix:', data[:40].hex())
        if len(data) > 8 and data[8] == 4:
            decoded = decode(frame)
            print('[FACT] New announcement:', decoded)
            assert decoded['name'] == 'disp0-service'
            assert not self.serv_map, 'Unexpected repeated announcement; stop'
            srv = self.serv_names[decoded['name']](self)
            srv.init(decoded['properties'])
            srv.chan = decoded['queue_channel']
            self.chan_map[srv.chan] = srv
            self.serv_map[decoded['name']] = srv
            self.disp0 = srv
            return
        return old_handler(self, data)
    EPICEndpoint.handle_ipc = handle
    signal.alarm(180) # includes slow UART transfers for RTKit buffers/page tables
    ASC.boot(dcp)
    dcp.mgmt.start()
    dcp.mgmt.wait_boot(30)
    print('[FACT] DCP management handshake complete')
    dcp.start_ep(0x23)
    dcp.iboot.wait_for('disp0')
    print('[FACT] AFK sizes:', dcp.iboot.txq.block_size, dcp.iboot.rxq.block_size)
    print('[DESIGN] Sending exactly one legacy getModeCount request; no modeset')
    signal.alarm(30)
    status = dcp.iboot.disp0.getModeCount()
    print('[FACT] Legacy request response:', status)
    signal.alarm(30)
    dcp.stop(0x10)
    print('[FACT] DCP quiesced')

finally:
    signal.alarm(0)
    iface.dev.close()
PY
