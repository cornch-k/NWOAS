#!/bin/bash
# S6-D9: query HDMI timing/color lists with the D8-validated in-band envelope.
# PASS: well-formed lists consistent with getModeCount. No display writes.
# Exact post-D8 state only, zero reboots, no DCP reset or allocations.
# Includes the one syslog acknowledgment captured by the D8 runner.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-list-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import os,signal,json,subprocess
from pathlib import Path
from m1n1.proxy import UartInterface,M1N1Proxy
from tahoe_afk_link import TahoeLink,TahoeIBoot,decode_packet
fixture=Path('/Volumes/X31/NWOAS/experiments/tahoe-afk-20260906/d8-response.bin').read_bytes()
decoded=decode_packet(fixture[8:])
assert (decoded['sequence'],decoded['interface'],decoded['kind'],decoded['category'])==(5,3,192,2)
for size in range(len(fixture)-8):
    try: decode_packet(fixture[8:8+size])
    except ValueError: pass
    else: raise AssertionError('Truncation accepted')
port='/dev/cu.usbmodemC07HL05SQ6NY1'
assert not subprocess.run(['/usr/sbin/lsof','-t',port],capture_output=True).stdout.strip()
def deadline(*a): raise TimeoutError('List experiment deadline')
signal.signal(signal.SIGALRM,deadline);signal.alarm(40)
i=UartInterface(port+':115200');i.dev.timeout=3;i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP);i.reply(i.REQ_NOP);p=M1N1Proxy(i)
    link=TahoeLink(i,p,(384,384,384,384),os.environ['NWOAS_PROTOCOL_LOG'])
    link.syslog(0x50000000000001)
    service=TahoeIBoot(link)
    hpd,nt,nc=service.getModeCount()
    timing=service.getTimingModes()
    color=service.getColorModes()
    assert hpd and len(timing)==nt and len(color)==nc
    def plain(c): return {k:int(v) for k,v in c.items() if not k.startswith('_')}
    result=dict(hpd=hpd,timing=[plain(c) for c in timing],color=[plain(c) for c in color],pointers=link.pointers())
    print('[FACT]',json.dumps(result,indent=2))
    Path(os.environ['NWOAS_PROTOCOL_LOG']+'.json').write_text(json.dumps(result,indent=2))
finally:
    signal.alarm(0);i.dev.close()
PY
