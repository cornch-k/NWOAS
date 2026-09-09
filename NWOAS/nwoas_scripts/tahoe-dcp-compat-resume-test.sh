#!/bin/bash
# S6-D6 continuation: host parser stopped before sending any request.
# Reattach to that exact live ring; no reboot/reset/second AFK initialization.
# Verify saved frame and ring pointers before allocating separate host buffers.
# Send ONE existing read-only mode-count request, capture response or timeout.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-compat-resume-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
from pathlib import Path
import os, struct, signal, subprocess
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils
from m1n1.hw.dart import DART
from m1n1.fw.dcp.iboot import DCPIBootClient, DCPIBootEndpoint, DCPIBootService
from m1n1.fw.afk.rbep import AFKRingBuf
from m1n1.fw.asc.syslog import ASCSysLogEndpoint
from m1n1.fw.asc.ioreporting import ASCIOReportingEndpoint
from decode_tahoe_announce import decode
root = Path('/Volumes/X31/NWOAS')
frame = (root/'nwoas_scripts/logs/tahoe-dcp-compat-20260906-224851.R8x5nC.msg1.bin').read_bytes()
record = decode(frame)
owners = subprocess.run(['/usr/sbin/lsof','-t','/dev/cu.debug-console'],capture_output=True)
assert owners.returncode in (0,1) and not owners.stdout.strip() and not owners.stderr.strip()
def deadline(signum, stack):
    raise TimeoutError('bounded compatibility experiment deadline')
signal.signal(signal.SIGALRM, deadline)
signal.alarm(120)
iface = UartInterface('/dev/cu.debug-console:115200')
iface.dev.timeout = 5
iface.dev.write_timeout = 3
try:
    iface.cmd(iface.REQ_NOP); iface.reply(iface.REQ_NOP)
    p = M1N1Proxy(iface)
    assert p.get_base() == 0x803a84000, 'Boot changed; cannot resume'
    shared = 0x80d224000
    assert iface.readmem(shared + 0x4180, len(frame)) == frame, 'Live announcement changed'
    assert (p.read32(shared+128), p.read32(shared+256)) == (0,0)
    assert (p.read32(shared+0x4080), p.read32(shared+0x4100)) == (256,256)
    u = ProxyUtils(p)
    # The old Python process used only the first few MiB of its private heap.
    # Reserve 16MiB to avoid overwriting its live RTKit buffers/page tables.
    reserve = u.malloc(0x1000000)
    assert reserve + 0x1000000 > shared + 0x8000
    assert u.adt.model == 'Macmini9,1'
    dart = DART.from_adt(u, '/arm-io/dart-dcp', iova_range=(0x81000000,0x82000000))
    assert dart.iotranslate(0, 0x81000000, 4) == [(None,4)]
    dcp = DCPIBootClient(u, u.adt['/arm-io/dcp'].get_reg(0)[0], dart)
    dcp.dva_offset = u.adt['/arm-io/dcp'][0].asc_dram_mask
    log_ep = ASCSysLogEndpoint(dcp,2)
    log_ep.count, log_ep.entrysize = 63,128
    log_ep.iobuffer, log_ep.iobuffer_dva = 0x80d220000,0xf80028000
    log_ep.started = True
    dcp.add_ep(2,log_ep)
    report_ep = ASCIOReportingEndpoint(dcp,4)
    report_ep.iobuffer, report_ep.iobuffer_dva, report_ep.bufsize = 0x80d21c000,0xf80024000,0x4000
    dcp.add_ep(4,report_ep)
    ep = DCPIBootEndpoint(dcp,0x23)
    ep.iobuffer, ep.iobuffer_dva = shared,0xf8002c000
    ep.txq, ep.rxq = AFKRingBuf(ep,shared,0x4000),AFKRingBuf(ep,shared+0x4000,0x4000)
    for ring in (ep.txq,ep.rxq):
        ring.rptr,ring.wptr = ring.get_rptr(),ring.get_wptr()
    ep.alive = ep.started = True
    dcp.add_ep(0x23,ep)
    old_handler = ep.handle_ipc
    def capture(data):
        packet = struct.pack('<4sI',b'IOP ',len(data)-8)+data
        Path(os.environ['NWOAS_PROTOCOL_LOG']+'.response.bin').write_bytes(packet)
        print('[FACT] Raw response:',packet.hex())
        if data[8] != 2:
            raise RuntimeError('New response envelope captured; not interpreted as legacy')
        return old_handler(data)
    ep.handle_ipc = capture
    srv = DCPIBootService(ep)
    srv.init(record['properties'])
    srv.chan = record['queue_channel']
    ep.disp0 = srv
    ep.chan_map[srv.chan] = srv
    ep.serv_map[record['name']] = srv
    print('[DESIGN] Sending ONE legacy getModeCount request after verified reattach')
    signal.alarm(30)
    print('[FACT] Mode-count response:',srv.getModeCount())
finally:
    signal.alarm(0)
    iface.dev.close()
PY
