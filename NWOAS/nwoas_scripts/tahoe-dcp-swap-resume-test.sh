#!/bin/bash
# S6-D27 parser-resume: no command is repeated. Reattach to the exact state
# after the already-successful swapBegin response, then send op16 and op18.
# No reboot, kmutil, mode/power command, PMGR reset, or guest boot.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-swap-resume-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import json,os,signal,struct,subprocess,time
from pathlib import Path
from construct import Container
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.fw.dcp.iboot import IBootLayerInfo
from tahoe_afk_link import TahoeLink,TahoeIBoot

port='/dev/cu.usbmodemC07HL05SQ6NY1'; prefix=os.environ['NWOAS_PROTOCOL_LOG']
base=34425208832; shared=34584231936; syslog=34584215552; asc=9424601088
expected=(768,768,2944,2944)
assert not subprocess.run(['/usr/sbin/lsof','-t',port],capture_output=True).stdout.strip()
def deadline(*a): raise TimeoutError('D27 resume deadline')
signal.signal(signal.SIGALRM,deadline); signal.alarm(35)
i=UartInterface(port+':115200'); i.dev.timeout=3; i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP); i.reply(i.REQ_NOP); p=M1N1Proxy(i)
    link=TahoeLink(i,p,expected,prefix,boot_base=base,shared=shared,
                   syslog_base=syslog,asc_base=asc)
    link.sequence=6; link.command_sequence=6; service=TahoeIBoot(link)
    layer=Container(planes=[Container(addr=0x13dc000,stride=5120,addr_format=1),Container(),Container()],
                    plane_cnt=1,width=1280,height=720,surface_fmt=1,colorspace=2,eotf=1,transform=0)
    rect=struct.pack('<8I',1280,720,0,0,1280,720,0,0)
    set_layer=bytes(8)+IBootLayerInfo.build(layer)+bytes(8)+rect+bytes(4)
    assert len(set_layer)==216
    service.send_cmd(16,set_layer,replen=128)
    service.send_cmd(18,bytes(12),replen=128)
    print('[FACT] Existing swap_id 2: setLayer/end ACK')
    end=time.monotonic()+15
    while time.monotonic()<end:
        assert not link.work(), 'Unexpected response outside command'
        time.sleep(.01)
    state=dict(boot_base=base,shared=shared,syslog_base=syslog,asc_base=asc,
               pointers=link.pointers(),sequence=link.sequence,
               command_sequence=link.command_sequence,swap_id=2)
    Path(prefix+'.state.json').write_text(json.dumps(state,indent=2))
    print('[FACT] Final state',state)
finally:
    signal.alarm(0); i.dev.close()
PY
