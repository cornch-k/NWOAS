#!/bin/bash
# S6-D26: reattach only to the exact D25 state and test the missing
# AFK CLOSE -> endpoint shutdown -> RTKit AP quiesce -> IOP sleep order.
# No reboot, kmutil, framebuffer write, mode command, PMGR reset, or guest boot.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-close-sleep-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import hashlib,os,signal,subprocess
from pathlib import Path
from m1n1.proxy import UartInterface,M1N1Proxy
from tahoe_afk_link import TahoeLink

root=Path('/Volumes/X31/NWOAS')
port='/dev/cu.usbmodemC07HL05SQ6NY1'
expected=(896,896,2944,2944)
base=34423291904
shared=34582282240
syslog=34582265856
asc=9424601088
assert not subprocess.run(['/usr/sbin/lsof','-t',port],capture_output=True).stdout.strip()
binary=(root/'experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin').read_bytes()
assert hashlib.md5(binary).hexdigest()=='7bec0225edc101857be3b7f195dd7357'
def deadline(*a): raise TimeoutError('D26 deadline')
signal.signal(signal.SIGALRM,deadline); signal.alarm(30)
i=UartInterface(port+':115200'); i.dev.timeout=3; i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP); i.reply(i.REQ_NOP); p=M1N1Proxy(i)
    assert p.get_base()==base
    link=TahoeLink(i,p,expected,os.environ['NWOAS_PROTOCOL_LOG'],
                   boot_base=base,shared=shared,syslog_base=syslog,asc_base=asc)
    link.sequence,link.command_sequence=6,6
    print('[DESIGN] Sending CLOSE report, AFK shutdown, AP quiesce, IOP sleep')
    link.close_and_sleep()
    print('[FACT] CLOSE and all three control ACKs succeeded')
    print('[FACT] Final pointers',link.pointers())
    print('[FACT] CPU control/status',hex(p.read32(asc+0x44)),hex(p.read32(asc+0x48)))
finally:
    signal.alarm(0); i.dev.close()
PY
